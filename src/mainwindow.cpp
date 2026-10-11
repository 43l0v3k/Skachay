#include "mainwindow.h"

#include "dialogs.h"
#include "downloaditem.h"
#include "downloadmanager.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QToolBar>
#include <QUrl>

namespace {

// Рисуем иконку кодом, чтобы не таскать бинарные ресурсы в репозитории.
QIcon makeIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0x2d, 0x7d, 0xf6));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(size * 0.04, size * 0.04, size * 0.92, size * 0.92));
        p.setBrush(Qt::white);
        const double c = size / 2.0;
        QPolygonF arrow;
        arrow << QPointF(c - size * 0.10, size * 0.22) << QPointF(c + size * 0.10, size * 0.22)
              << QPointF(c + size * 0.10, size * 0.46) << QPointF(c + size * 0.24, size * 0.46)
              << QPointF(c, size * 0.74) << QPointF(c - size * 0.24, size * 0.46)
              << QPointF(c - size * 0.10, size * 0.46);
        p.drawPolygon(arrow);
        p.drawRect(QRectF(size * 0.26, size * 0.78, size * 0.48, size * 0.07));
        icon.addPixmap(pm);
    }
    return icon;
}

class ProgressDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (index.column() != DownloadManager::ColProgress) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionProgressBar bar;
        bar.rect = option.rect.adjusted(4, 4, -4, -4);
        bar.minimum = 0;
        bar.maximum = 100;
        bar.progress = index.data(Qt::UserRole).toInt();
        bar.text = QString::number(bar.progress) + QStringLiteral("%");
        bar.textVisible = true;
        bar.state = QStyle::State_Enabled;
        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &bar, painter);
    }
};

bool looksLikeDownload(const QString &text)
{
    const QString t = text.trimmed();
    if (t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char(' ')) || t.size() > 2000)
        return false;
    const QUrl u(t);
    if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")
                         && u.scheme() != QLatin1String("ftp")))
        return false;
    static const QStringList exts = {
        "zip", "rar", "7z", "gz", "xz", "bz2", "tar", "iso", "img", "exe", "msi", "deb", "rpm",
        "apk", "dmg", "pkg", "appimage", "mp4", "mkv", "avi", "mov", "webm", "mp3", "flac", "wav",
        "pdf", "epub", "djvu", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "torrent"};
    return exts.contains(QFileInfo(u.path()).suffix().toLower());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_mgr(new DownloadManager(this)), m_view(new QTableView(this))
{
    setWindowTitle(QStringLiteral("Скачай"));
    setWindowIcon(makeIcon());
    resize(900, 480);
    buildUi();
    applySettings();
    m_mgr->load(); // незавершённые после перезапуска остаются на паузе
    updateActions();

    connect(m_mgr, &DownloadManager::downloadFinished, this, [this](DownloadItem *it) {
        if (m_tray)
            m_tray->showMessage(QStringLiteral("Загрузка завершена"), it->fileName(),
                                QSystemTrayIcon::Information, 4000);
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::onClipboardChanged);
}

void MainWindow::buildUi()
{
    m_view->setModel(m_mgr);
    m_view->setItemDelegate(new ProgressDelegate(m_view));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setShowGrid(false);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setColumnWidth(DownloadManager::ColName, 280);
    m_view->setColumnWidth(DownloadManager::ColSize, 150);
    m_view->setColumnWidth(DownloadManager::ColProgress, 130);
    m_view->setColumnWidth(DownloadManager::ColSpeed, 100);
    m_view->setColumnWidth(DownloadManager::ColEta, 90);
    setCentralWidget(m_view);

    auto *tb = addToolBar(QStringLiteral("Главная"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_actAdd = tb->addAction(QStringLiteral("Добавить"), this, [this]() { showAddDialog(); });
    m_actAdd->setShortcut(QKeySequence::New);
    m_actPause = tb->addAction(QStringLiteral("Пауза"), this, &MainWindow::pauseSelected);
    m_actResume = tb->addAction(QStringLiteral("Продолжить"), this, &MainWindow::resumeSelected);
    m_actRemove = tb->addAction(QStringLiteral("Удалить"), this, [this]() { removeSelected(false); });
    m_actRemove->setShortcut(QKeySequence::Delete);
    tb->addSeparator();
    m_actOpen = tb->addAction(QStringLiteral("Открыть файл"), this, &MainWindow::openSelectedFile);
    m_actFolder = tb->addAction(QStringLiteral("Открыть папку"), this, &MainWindow::openSelectedFolder);
    tb->addSeparator();
    tb->addAction(QStringLiteral("Пауза всем"), this, [this]() { m_mgr->pauseAll(); });
    tb->addAction(QStringLiteral("Продолжить все"), this, [this]() { m_mgr->resumeAll(); });
    tb->addSeparator();
    tb->addAction(QStringLiteral("Настройки"), this, &MainWindow::showSettings);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { updateActions(); });
    connect(m_mgr, &QAbstractItemModel::dataChanged, this, [this]() { updateActions(); });
    connect(m_view, &QTableView::doubleClicked, this, [this]() { openSelectedFile(); });

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex idx = m_view->indexAt(pos);
        if (idx.isValid())
            m_view->selectRow(idx.row());
        QMenu menu(this);
        menu.addAction(m_actPause);
        menu.addAction(m_actResume);
        menu.addSeparator();
        menu.addAction(m_actOpen);
        menu.addAction(m_actFolder);
        menu.addSeparator();
        menu.addAction(m_actRemove);
        QAction *rmFile = menu.addAction(QStringLiteral("Удалить вместе с файлом"));
        connect(rmFile, &QAction::triggered, this, [this]() { removeSelected(true); });
        menu.exec(m_view->viewport()->mapToGlobal(pos));
    });

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(makeIcon(), this);
        auto *menu = new QMenu(this);
        menu->addAction(QStringLiteral("Показать"), this, [this]() {
            showNormal();
            raise();
            activateWindow();
        });
        menu->addAction(QStringLiteral("Добавить ссылку…"), this, [this]() { showAddDialog(); });
        menu->addSeparator();
        menu->addAction(QStringLiteral("Выход"), this, &MainWindow::quitApp);
        m_tray->setContextMenu(menu);
        m_tray->setToolTip(QStringLiteral("Скачай"));
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason r) {
                    if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
                        showNormal();
                        raise();
                        activateWindow();
                    }
                });
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
            if (!m_clipboardCandidate.isEmpty()) {
                showAddDialog(m_clipboardCandidate);
                m_clipboardCandidate.clear();
            }
        });
        m_tray->show();
    }
}

QString MainWindow::downloadDir() const
{
    QSettings s;
    return s.value(QStringLiteral("downloadDir"),
                   QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        .toString();
}

void MainWindow::applySettings()
{
    QSettings s;
    m_mgr->setMaxConcurrent(s.value(QStringLiteral("maxConcurrent"), 3).toInt());
    m_mgr->setMaxSegments(s.value(QStringLiteral("maxSegments"), 8).toInt());
}

int MainWindow::currentRow() const
{
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void MainWindow::updateActions()
{
    DownloadItem *it = m_mgr->itemAt(currentRow());
    const bool has = it != nullptr;
    const auto st = has ? it->state() : DownloadItem::Finished;
    m_actPause->setEnabled(has && (it->isActive() || st == DownloadItem::Queued));
    m_actResume->setEnabled(has && (st == DownloadItem::Paused || st == DownloadItem::Failed));
    m_actRemove->setEnabled(has);
    m_actOpen->setEnabled(has && st == DownloadItem::Finished);
    m_actFolder->setEnabled(has);
}

void MainWindow::showAddDialog(const QString &prefillUrl)
{
    QString prefill = prefillUrl;
    if (prefill.isEmpty()) {
        const QString clip = QApplication::clipboard()->text().trimmed();
        if (looksLikeDownload(clip))
            prefill = clip;
    }
    showNormal();
    raise();
    activateWindow();
    AddUrlDialog dlg(downloadDir(), prefill, this);
    if (dlg.exec() == QDialog::Accepted)
        addUrls(dlg.urls(), dlg.directory());
}

void MainWindow::addUrls(const QStringList &urls, const QString &dir)
{
    for (const QString &u : urls)
        m_mgr->add(QUrl(u), dir.isEmpty() ? downloadDir() : dir);
}

void MainWindow::onClipboardChanged()
{
    QSettings s;
    if (!s.value(QStringLiteral("watchClipboard"), true).toBool() || !m_tray)
        return;
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text == m_lastClipboard || !looksLikeDownload(text))
        return;
    m_lastClipboard = text;
    m_clipboardCandidate = text;
    m_tray->showMessage(QStringLiteral("Скачать ссылку?"),
                        QStringLiteral("Нажми, чтобы добавить загрузку:\n") + text.left(120),
                        QSystemTrayIcon::Information, 6000);
}

void MainWindow::pauseSelected()
{
    if (DownloadItem *it = m_mgr->itemAt(currentRow()))
        it->pause();
}

void MainWindow::resumeSelected()
{
    if (DownloadItem *it = m_mgr->itemAt(currentRow())) {
        it->enqueue();
        m_mgr->schedule();
    }
}

void MainWindow::removeSelected(bool deleteFile)
{
    const int row = currentRow();
    DownloadItem *it = m_mgr->itemAt(row);
    if (!it)
        return;
    if (deleteFile
        && QMessageBox::question(this, QStringLiteral("Удалить"),
                                 QStringLiteral("Удалить файл с диска?\n%1").arg(it->finalPath()))
               != QMessageBox::Yes)
        return;
    m_mgr->remove(row, deleteFile);
}

void MainWindow::openSelectedFile()
{
    DownloadItem *it = m_mgr->itemAt(currentRow());
    if (it && it->state() == DownloadItem::Finished)
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->finalPath()));
}

void MainWindow::openSelectedFolder()
{
    if (DownloadItem *it = m_mgr->itemAt(currentRow()))
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->dir()));
}

void MainWindow::showSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        applySettings();
}

void MainWindow::quitApp()
{
    m_quitting = true;
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_tray && m_tray->isVisible() && !m_quitting) {
        hide(); // в трей, загрузки продолжаются
        event->ignore();
        return;
    }
    qApp->quit(); // без трея окно — единственный интерфейс, закрытие = выход
    event->accept();
}
