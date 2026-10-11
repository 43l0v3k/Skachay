#include "downloadmanager.h"

#include "downloaditem.h"
#include "util.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QStandardPaths>

DownloadManager::DownloadManager(QObject *parent)
    : QAbstractTableModel(parent), m_nam(new QNetworkAccessManager(this))
{
    m_saveTimer.setInterval(5000);
    connect(&m_saveTimer, &QTimer::timeout, this, &DownloadManager::save);
    m_saveTimer.start();
}

DownloadManager::~DownloadManager()
{
    for (DownloadItem *it : qAsConst(m_items))
        if (it->isActive())
            it->pause();
    save();
}

QString DownloadManager::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("downloads.json"));
}

void DownloadManager::load()
{
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    beginResetModel();
    for (const QJsonValue &v : arr) {
        DownloadItem *it = DownloadItem::fromJson(m_nam, v.toObject(), this);
        it->setMaxSegments(m_maxSegments);
        m_nextId = qMax(m_nextId, it->id() + 1);
        attach(it);
        m_items.append(it);
    }
    endResetModel();
}

void DownloadManager::save()
{
    QJsonArray arr;
    for (DownloadItem *it : qAsConst(m_items)) {
        if (it->isActive())
            it->flush(); // чтобы позиции сегментов не обгоняли данные на диске
        arr.append(it->toJson());
    }
    QSaveFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}

void DownloadManager::attach(DownloadItem *item)
{
    connect(item, &DownloadItem::changed, this, &DownloadManager::onItemChanged);
    connect(item, &DownloadItem::stateChanged, this, &DownloadManager::onItemStateChanged);
}

DownloadItem *DownloadManager::add(const QUrl &url, const QString &dir)
{
    auto *it = new DownloadItem(m_nam, url, dir, this);
    it->setId(m_nextId++);
    it->setMaxSegments(m_maxSegments);
    attach(it);

    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(it);
    endInsertRows();

    it->enqueue(); // Queued уже по умолчанию, но вызов безвреден
    QTimer::singleShot(0, this, &DownloadManager::schedule);
    save();
    return it;
}

DownloadItem *DownloadManager::itemAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? m_items.at(row) : nullptr;
}

void DownloadManager::remove(int row, bool deleteFile)
{
    DownloadItem *it = itemAt(row);
    if (!it)
        return;
    const QString finalPath = it->finalPath();
    const bool finished = it->state() == DownloadItem::Finished;
    it->cancel();
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    endRemoveRows();
    if (deleteFile && finished)
        QFile::remove(finalPath);
    it->deleteLater();
    save();
    QTimer::singleShot(0, this, &DownloadManager::schedule);
}

void DownloadManager::pauseAll()
{
    for (DownloadItem *it : qAsConst(m_items))
        it->pause();
}

void DownloadManager::resumeAll()
{
    for (DownloadItem *it : qAsConst(m_items))
        it->enqueue();
    schedule();
}

void DownloadManager::setMaxConcurrent(int n)
{
    m_maxConcurrent = qBound(1, n, 20);
    schedule();
}

void DownloadManager::setMaxSegments(int n)
{
    m_maxSegments = qBound(1, n, 16);
    for (DownloadItem *it : qAsConst(m_items))
        it->setMaxSegments(m_maxSegments);
}

// ------------------------------------------------------------------- queue

void DownloadManager::schedule()
{
    if (m_scheduling)
        return;
    m_scheduling = true;
    int active = 0;
    for (DownloadItem *it : qAsConst(m_items))
        if (it->isActive())
            ++active;
    for (DownloadItem *it : qAsConst(m_items)) {
        if (active >= m_maxConcurrent)
            break;
        if (it->state() == DownloadItem::Queued) {
            it->start();
            if (it->isActive())
                ++active;
        }
    }
    m_scheduling = false;
}

void DownloadManager::onItemChanged()
{
    auto *it = qobject_cast<DownloadItem *>(sender());
    const int row = m_items.indexOf(it);
    if (row >= 0)
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void DownloadManager::onItemStateChanged()
{
    auto *it = qobject_cast<DownloadItem *>(sender());
    if (it && it->state() == DownloadItem::Finished)
        emit downloadFinished(it);
    else if (it && it->state() == DownloadItem::Failed)
        emit downloadFailed(it);
    QTimer::singleShot(0, this, &DownloadManager::schedule);
    save();
}

// ------------------------------------------------------------------- model

int DownloadManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

int DownloadManager::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString DownloadManager::stateText(DownloadItem *it)
{
    switch (it->state()) {
    case DownloadItem::Queued:
        return QStringLiteral("В очереди");
    case DownloadItem::Probing:
        return QStringLiteral("Подключение…");
    case DownloadItem::Downloading:
        return QStringLiteral("Загрузка");
    case DownloadItem::Paused:
        return QStringLiteral("Пауза");
    case DownloadItem::Finished:
        return QStringLiteral("Готово");
    case DownloadItem::Failed:
        return QStringLiteral("Ошибка: ") + it->error();
    }
    return QString();
}

QVariant DownloadManager::data(const QModelIndex &index, int role) const
{
    DownloadItem *it = itemAt(index.row());
    if (!it)
        return QVariant();

    if (role == Qt::UserRole) // процент для делегата прогресса
        return it->percent();

    if (role == Qt::ToolTipRole)
        return it->url().toString();

    if (role != Qt::DisplayRole)
        return QVariant();

    switch (index.column()) {
    case ColName:
        return it->fileName().isEmpty() ? it->url().toString() : it->fileName();
    case ColSize:
        return it->total() >= 0
                   ? util::humanSize(it->downloaded()) + QStringLiteral(" / ")
                         + util::humanSize(it->total())
                   : util::humanSize(it->downloaded());
    case ColProgress:
        return it->percent();
    case ColSpeed:
        return it->state() == DownloadItem::Downloading ? util::humanSpeed(it->speed())
                                                        : QString();
    case ColEta:
        return util::humanTime(it->etaSeconds());
    case ColStatus:
        return stateText(it);
    }
    return QVariant();
}

QVariant DownloadManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case ColName:
        return QStringLiteral("Файл");
    case ColSize:
        return QStringLiteral("Размер");
    case ColProgress:
        return QStringLiteral("Прогресс");
    case ColSpeed:
        return QStringLiteral("Скорость");
    case ColEta:
        return QStringLiteral("Осталось");
    case ColStatus:
        return QStringLiteral("Статус");
    }
    return QVariant();
}
