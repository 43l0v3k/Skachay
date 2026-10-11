#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

struct Segment {
    qint64 start = 0;
    qint64 end = -1; // включительно; -1 = размер неизвестен
    qint64 pos = 0;  // следующий байт, который надо скачать
    int retries = 0;

    bool done() const { return end >= 0 && pos > end; }
};

// Одна загрузка: определяет размер, режет файл на сегменты, качает их
// параллельно (HTTP Range), умеет паузу/докачку и сохранение состояния.
class DownloadItem : public QObject
{
    Q_OBJECT
public:
    enum State { Queued, Probing, Downloading, Paused, Finished, Failed };
    Q_ENUM(State)

    DownloadItem(QNetworkAccessManager *nam, const QUrl &url, const QString &dir,
                 QObject *parent = nullptr);

    static DownloadItem *fromJson(QNetworkAccessManager *nam, const QJsonObject &o,
                                  QObject *parent = nullptr);
    QJsonObject toJson() const;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    QUrl url() const { return m_url; }
    QString dir() const { return m_dir; }
    QString fileName() const { return m_fileName; }
    QString finalPath() const;
    QString partPath() const;
    qint64 total() const { return m_total; }
    qint64 downloaded() const;
    bool resumable() const { return m_resumable; }
    State state() const { return m_state; }
    QString error() const { return m_error; }
    qint64 speed() const { return m_speed; }
    qint64 etaSeconds() const;
    int percent() const;

    bool isActive() const { return m_state == Probing || m_state == Downloading; }

    void setMaxSegments(int n) { m_maxSegments = qBound(1, n, 16); }

public slots:
    void enqueue();   // поставить в очередь (Queued)
    void start();     // реально начать (вызывает менеджер)
    void pause();
    void cancel();    // остановить и удалить .part
    void flush();

signals:
    void changed();
    void stateChanged();

private:
    QNetworkRequest makeRequest() const;
    void setState(State s);
    void fail(const QString &message);

    void probe();
    void onProbeHeaders();
    void finishProbe();
    void beginTransfer();
    void buildSegments();
    void startSegment(int idx);
    void onReadyRead(QNetworkReply *r);
    void onSegmentFinished(QNetworkReply *r);
    void abortReplies();
    void finishDownload();
    void tick();

    static QString sanitize(QString name);
    static QString nameFromDisposition(const QByteArray &header);
    QString uniqueName(const QString &name) const;

    QNetworkAccessManager *m_nam;
    int m_id = 0;
    QUrl m_url;
    QUrl m_finalUrl;
    QString m_dir;
    QString m_fileName;
    qint64 m_total = -1;
    bool m_resumable = false;
    State m_state = Queued;
    QString m_error;
    int m_maxSegments = 8;

    QVector<Segment> m_segments;
    QList<QNetworkReply *> m_replies;
    QFile *m_file = nullptr;

    QNetworkReply *m_probe = nullptr;
    bool m_probeHandled = false;

    int m_generation = 0; // растёт при паузе/отмене, чтобы отложенные повторы устаревали

    QTimer m_tick;
    qint64 m_bytesSinceTick = 0;
    qint64 m_speed = 0;
    qint64 m_smoothSpeed = 0;
};
