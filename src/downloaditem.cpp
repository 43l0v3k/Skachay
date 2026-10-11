#include "downloaditem.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {
const char *kUserAgent =
    "Mozilla/5.0 (Windows NT 6.1; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/109.0 Safari/537.36";
const qint64 kMinSplitSize = 2 * 1024 * 1024; // меньше 2 МБ на сегменты не режем
const qint64 kMinSegmentSize = 1024 * 1024;
const int kMaxRetries = 5;
} // namespace

DownloadItem::DownloadItem(QNetworkAccessManager *nam, const QUrl &url, const QString &dir,
                           QObject *parent)
    : QObject(parent), m_nam(nam), m_url(url), m_dir(dir)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &DownloadItem::tick);
}

// ---------------------------------------------------------------- persistence

DownloadItem *DownloadItem::fromJson(QNetworkAccessManager *nam, const QJsonObject &o,
                                     QObject *parent)
{
    auto *it = new DownloadItem(nam, QUrl(o["url"].toString()), o["dir"].toString(), parent);
    it->m_id = o["id"].toInt();
    it->m_finalUrl = QUrl(o["finalUrl"].toString());
    it->m_fileName = o["fileName"].toString();
    it->m_total = qint64(o["total"].toDouble(-1));
    it->m_resumable = o["resumable"].toBool();
    it->m_error = o["error"].toString();

    const QJsonArray segs = o["segments"].toArray();
    for (const QJsonValue &v : segs) {
        const QJsonArray a = v.toArray();
        if (a.size() < 3)
            continue;
        Segment s;
        s.start = qint64(a[0].toDouble());
        s.end = qint64(a[1].toDouble());
        s.pos = qint64(a[2].toDouble());
        it->m_segments.append(s);
    }

    State st = State(o["state"].toInt(Paused));
    if (st == Finished || st == Failed)
        it->m_state = st;
    else
        it->m_state = Paused; // всё, что не закончено, после перезапуска на паузе
    return it;
}

QJsonObject DownloadItem::toJson() const
{
    QJsonObject o;
    o["id"] = m_id;
    o["url"] = m_url.toString();
    o["finalUrl"] = m_finalUrl.toString();
    o["dir"] = m_dir;
    o["fileName"] = m_fileName;
    o["total"] = double(m_total);
    o["resumable"] = m_resumable;
    o["state"] = int(m_state);
    o["error"] = m_error;
    QJsonArray segs;
    for (const Segment &s : m_segments)
        segs.append(QJsonArray{double(s.start), double(s.end), double(s.pos)});
    o["segments"] = segs;
    return o;
}

// ------------------------------------------------------------------- getters

QString DownloadItem::finalPath() const
{
    return QDir(m_dir).filePath(m_fileName);
}

QString DownloadItem::partPath() const
{
    return finalPath() + QStringLiteral(".part");
}

qint64 DownloadItem::downloaded() const
{
    if (m_state == Finished && m_total >= 0)
        return m_total;
    qint64 sum = 0;
    for (const Segment &s : m_segments)
        sum += qMax<qint64>(0, s.pos - s.start);
    return sum;
}

qint64 DownloadItem::etaSeconds() const
{
    if (m_state != Downloading || m_total < 0 || m_smoothSpeed <= 0)
        return -1;
    return (m_total - downloaded()) / m_smoothSpeed;
}

int DownloadItem::percent() const
{
    if (m_state == Finished)
        return 100;
    if (m_total <= 0)
        return 0;
    return int(downloaded() * 100 / m_total);
}

// ------------------------------------------------------------------ control

void DownloadItem::enqueue()
{
    if (m_state == Paused || m_state == Failed) {
        m_error.clear();
        setState(Queued);
    }
}

void DownloadItem::start()
{
    if (m_state != Queued)
        return;
    m_error.clear();
    m_bytesSinceTick = 0;
    m_speed = m_smoothSpeed = 0;
    m_tick.start();
    if (m_fileName.isEmpty() || m_segments.isEmpty())
        probe();
    else
        beginTransfer();
}

void DownloadItem::pause()
{
    if (m_state == Queued) {
        setState(Paused);
        return;
    }
    if (!isActive())
        return;
    ++m_generation;
    abortReplies();
    if (m_probe) {
        m_probe->disconnect(this);
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    flush();
    if (!m_resumable) {
        // без Range докачать нельзя — начнём сначала
        for (Segment &s : m_segments)
            s.pos = s.start;
        if (m_file) {
            m_file->resize(0);
        }
    }
    m_tick.stop();
    m_speed = m_smoothSpeed = 0;
    setState(Paused);
}

void DownloadItem::cancel()
{
    ++m_generation;
    if (m_state != Finished)
        m_state = Paused; // без сигналов: отложенные вызовы не должны ничего перезапустить
    abortReplies();
    if (m_probe) {
        m_probe->disconnect(this);
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    m_tick.stop();
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (!m_fileName.isEmpty() && m_state != Finished)
        QFile::remove(partPath());
}

void DownloadItem::flush()
{
    if (m_file && m_file->isOpen())
        m_file->flush();
}

// ------------------------------------------------------------------ internals

QNetworkRequest DownloadItem::makeRequest() const
{
    QNetworkRequest req(m_finalUrl.isEmpty() ? m_url : m_finalUrl);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "*/*");
    req.setRawHeader("Accept-Encoding", "identity"); // нужны сырые байты, без gzip
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    req.setTransferTimeout(30000); // 30 с без данных = обрыв, дальше сработает повтор
#endif
    return req;
}

void DownloadItem::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
    emit changed();
}

void DownloadItem::fail(const QString &message)
{
    ++m_generation;
    abortReplies();
    if (m_probe) {
        m_probe->disconnect(this);
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    flush();
    m_tick.stop();
    m_speed = m_smoothSpeed = 0;
    m_error = message;
    setState(Failed);
}

void DownloadItem::abortReplies()
{
    const QList<QNetworkReply *> replies = m_replies;
    m_replies.clear();
    for (QNetworkReply *r : replies) {
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void DownloadItem::tick()
{
    m_speed = m_bytesSinceTick;
    m_bytesSinceTick = 0;
    m_smoothSpeed = m_smoothSpeed == 0 ? m_speed : (m_smoothSpeed * 7 + m_speed * 3) / 10;
    emit changed();
}

// -------------------------------------------------------------------- probe

void DownloadItem::probe()
{
    setState(Probing);
    m_probeHandled = false;
    QNetworkRequest req = makeRequest();
    req.setRawHeader("Range", "bytes=0-0"); // GET вместо HEAD: HEAD многие серверы ломают
    m_probe = m_nam->get(req);
    connect(m_probe, &QNetworkReply::metaDataChanged, this, &DownloadItem::onProbeHeaders);
    connect(m_probe, &QNetworkReply::finished, this, [this]() {
        if (!m_probe)
            return;
        if (!m_probeHandled) {
            onProbeHeaders();
            if (!m_probeHandled) {
                const QString err = m_probe->errorString();
                m_probe->disconnect(this);
                m_probe->deleteLater();
                m_probe = nullptr;
                fail(err);
            }
        }
    });
}

void DownloadItem::onProbeHeaders()
{
    if (!m_probe || m_probeHandled)
        return;
    const int code = m_probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 0 || (code >= 300 && code < 400))
        return; // ещё не пришли заголовки или идёт редирект

    if (code >= 400 && code != 416) {
        const QString msg = QStringLiteral("Сервер ответил: HTTP %1").arg(code);
        m_probeHandled = true;
        m_probe->disconnect(this);
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
        fail(msg);
        return;
    }

    m_finalUrl = m_probe->url();

    if (code == 206) {
        // Content-Range: bytes 0-0/12345
        const QByteArray cr = m_probe->rawHeader("Content-Range");
        const int slash = cr.lastIndexOf('/');
        bool ok = false;
        const qint64 t = slash >= 0 ? cr.mid(slash + 1).trimmed().toLongLong(&ok) : 0;
        m_total = ok ? t : -1;
        m_resumable = ok;
    } else if (code == 416) {
        m_total = 0; // Range не имеет смысла для пустого файла
        m_resumable = false;
    } else {
        bool ok = false;
        const qint64 t = m_probe->rawHeader("Content-Length").toLongLong(&ok);
        m_total = ok ? t : -1;
        m_resumable = false;
    }

    QString name = nameFromDisposition(m_probe->rawHeader("Content-Disposition"));
    if (name.isEmpty())
        name = m_finalUrl.fileName(); // уже декодировано
    name = sanitize(name);
    if (name.isEmpty())
        name = QStringLiteral("download");
    if (m_fileName.isEmpty())
        m_fileName = uniqueName(name);

    m_probeHandled = true;
    QMetaObject::invokeMethod(this, &DownloadItem::finishProbe, Qt::QueuedConnection);
}

void DownloadItem::finishProbe()
{
    if (m_probe) {
        m_probe->disconnect(this);
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    if (m_state != Probing)
        return; // пока ждали — поставили на паузу или отменили
    beginTransfer();
}

// ----------------------------------------------------------------- transfer

void DownloadItem::buildSegments()
{
    m_segments.clear();
    if (m_resumable && m_total >= kMinSplitSize) {
        const int n = int(qMin<qint64>(m_maxSegments, m_total / kMinSegmentSize));
        const qint64 chunk = m_total / n;
        for (int i = 0; i < n; ++i) {
            Segment s;
            s.start = s.pos = i * chunk;
            s.end = (i == n - 1) ? m_total - 1 : s.start + chunk - 1;
            m_segments.append(s);
        }
    } else {
        Segment s;
        s.start = s.pos = 0;
        s.end = m_total >= 0 ? m_total - 1 : -1;
        m_segments.append(s);
    }
}

void DownloadItem::beginTransfer()
{
    QDir().mkpath(m_dir);

    if (m_segments.isEmpty())
        buildSegments();

    // .part удалили, пока загрузка стояла: прогресс недействителен, качаем с нуля
    if (!m_file && !QFile::exists(partPath())) {
        for (Segment &s : m_segments)
            s.pos = s.start;
    }

    if (!m_file) {
        m_file = new QFile(partPath(), this);
    }
    if (!m_file->isOpen() && !m_file->open(QIODevice::ReadWrite)) {
        fail(QStringLiteral("Не удалось открыть файл: %1").arg(m_file->errorString()));
        return;
    }
    if (m_total > 0 && m_file->size() != m_total) {
        if (!m_file->resize(m_total)) {
            fail(QStringLiteral("Не удалось выделить место на диске (%1 байт)").arg(m_total));
            return;
        }
    }

    setState(Downloading);

    if (m_total == 0) { // пустой файл: качать нечего
        finishDownload();
        return;
    }

    bool anyStarted = false;
    for (int i = 0; i < m_segments.size(); ++i) {
        if (!m_segments[i].done()) {
            startSegment(i);
            anyStarted = true;
        }
    }
    if (!anyStarted)
        finishDownload();
}

void DownloadItem::startSegment(int idx)
{
    if (!m_resumable)
        m_segments[idx].pos = m_segments[idx].start; // сервер отдаст файл с нуля
    const Segment &s = m_segments[idx];
    QNetworkRequest req = makeRequest();
    if (m_resumable) {
        QByteArray range = "bytes=" + QByteArray::number(s.pos) + "-";
        if (s.end >= 0)
            range += QByteArray::number(s.end);
        req.setRawHeader("Range", range);
    }
    QNetworkReply *r = m_nam->get(req);
    r->setProperty("seg", idx);
    m_replies.append(r);
    connect(r, &QNetworkReply::readyRead, this, [this, r]() { onReadyRead(r); });
    connect(r, &QNetworkReply::finished, this, [this, r]() { onSegmentFinished(r); });
}

void DownloadItem::onReadyRead(QNetworkReply *r)
{
    if (m_state != Downloading || !m_file)
        return;
    const int idx = r->property("seg").toInt();
    if (idx < 0 || idx >= m_segments.size())
        return;

    if (m_resumable) {
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 429 || code >= 500)
            return; // временная ошибка сервера: onSegmentFinished сделает повтор
        if (code != 206) {
            fail(QStringLiteral("Сервер перестал поддерживать докачку (HTTP %1)").arg(code));
            return;
        }
    }

    Segment &s = m_segments[idx];
    QByteArray data = r->readAll();
    if (s.end >= 0) {
        const qint64 remaining = s.end - s.pos + 1;
        if (data.size() > remaining)
            data.truncate(int(remaining));
    }
    if (data.isEmpty())
        return;

    if (!m_file->seek(s.pos) || m_file->write(data) != data.size()) {
        fail(QStringLiteral("Ошибка записи на диск: %1").arg(m_file->errorString()));
        return;
    }
    s.pos += data.size();
    s.retries = 0;
    m_bytesSinceTick += data.size();
}

void DownloadItem::onSegmentFinished(QNetworkReply *r)
{
    if (!m_replies.removeOne(r)) {
        r->deleteLater();
        return;
    }
    const int idx = r->property("seg").toInt();
    const QNetworkReply::NetworkError err = r->error();
    const QString errText = r->errorString();

    // остаток данных, пришедший вместе с finished
    if (err == QNetworkReply::NoError && r->bytesAvailable() > 0)
        onReadyRead(r);
    r->deleteLater();

    if (m_state != Downloading || idx < 0 || idx >= m_segments.size())
        return;

    Segment &s = m_segments[idx];

    if (err == QNetworkReply::NoError) {
        if (s.end < 0) {
            // размер был неизвестен: сервер закончил отдавать — файл готов
            s.end = s.pos - 1;
            m_total = s.pos;
        }
        if (!s.done()) {
            // сервер закрыл соединение раньше времени — пробуем ещё раз
            if (++s.retries > kMaxRetries) {
                fail(QStringLiteral("Соединение закрыто раньше времени"));
                return;
            }
            const int gen = m_generation;
            QTimer::singleShot(1500, this, [this, idx, gen]() {
                if (gen == m_generation && m_state == Downloading && !m_segments[idx].done())
                    startSegment(idx);
            });
            return;
        }
    } else {
        if (++s.retries > kMaxRetries) {
            fail(errText);
            return;
        }
        const int gen = m_generation;
        QTimer::singleShot(2000, this, [this, idx, gen]() {
            if (gen == m_generation && m_state == Downloading && !m_segments[idx].done())
                startSegment(idx);
        });
        return;
    }

    bool allDone = true;
    for (const Segment &seg : qAsConst(m_segments))
        if (!seg.done())
            allDone = false;
    if (allDone && m_replies.isEmpty())
        finishDownload();
}

void DownloadItem::finishDownload()
{
    m_tick.stop();
    m_speed = m_smoothSpeed = 0;
    if (m_file) {
        m_file->flush();
        if (m_total >= 0 && m_file->size() != m_total)
            m_file->resize(m_total);
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    const QString dst = finalPath();
    if (QFile::exists(dst))
        QFile::remove(dst);
    if (!QFile::rename(partPath(), dst)) {
        fail(QStringLiteral("Не удалось переименовать %1").arg(partPath()));
        return;
    }
    setState(Finished);
}

// ------------------------------------------------------------------- naming

QString DownloadItem::sanitize(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")),
                 QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')))
        name.chop(1);
    if (name.size() > 200)
        name = name.left(200);
    return name;
}

QString DownloadItem::nameFromDisposition(const QByteArray &header)
{
    if (header.isEmpty())
        return QString();
    const QString h = QString::fromUtf8(header);

    // filename*=UTF-8''%D0%...
    QRegularExpression star(QStringLiteral("filename\\*\\s*=\\s*[^']*'[^']*'([^;]+)"),
                            QRegularExpression::CaseInsensitiveOption);
    auto m = star.match(h);
    if (m.hasMatch())
        return QUrl::fromPercentEncoding(m.captured(1).trimmed().toUtf8());

    QRegularExpression plain(QStringLiteral("filename\\s*=\\s*\"?([^\";]+)\"?"),
                             QRegularExpression::CaseInsensitiveOption);
    m = plain.match(h);
    if (m.hasMatch())
        return m.captured(1).trimmed();
    return QString();
}

QString DownloadItem::uniqueName(const QString &name) const
{
    QDir d(m_dir);
    if (!d.exists(name) && !d.exists(name + QStringLiteral(".part")))
        return name;
    const QFileInfo fi(name);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        const QString cand = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix);
        if (!d.exists(cand) && !d.exists(cand + QStringLiteral(".part")))
            return cand;
    }
    return name;
}
