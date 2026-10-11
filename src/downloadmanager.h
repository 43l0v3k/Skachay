#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QTimer>
#include <QUrl>

class DownloadItem;
class QNetworkAccessManager;

// Список загрузок + очередь + сохранение на диск. Одновременно является моделью для таблицы.
class DownloadManager : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName, ColSize, ColProgress, ColSpeed, ColEta, ColStatus, ColumnCount };

    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    DownloadItem *add(const QUrl &url, const QString &dir);
    DownloadItem *itemAt(int row) const;
    void remove(int row, bool deleteFile);

    void pauseAll();
    void resumeAll();

    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);
    void setMaxSegments(int n);

    void load();
    void save();

    // QAbstractTableModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void downloadFinished(DownloadItem *item);
    void downloadFailed(DownloadItem *item);

public slots:
    void schedule();

private slots:
    void onItemChanged();
    void onItemStateChanged();

private:
    void attach(DownloadItem *item);
    QString storagePath() const;
    static QString stateText(DownloadItem *it);

    QNetworkAccessManager *m_nam;
    QList<DownloadItem *> m_items;
    int m_maxConcurrent = 3;
    int m_maxSegments = 8;
    int m_nextId = 1;
    QTimer m_saveTimer;
    bool m_scheduling = false;
};
