#pragma once

#include <QMainWindow>

class DownloadManager;
class DownloadItem;
class QAction;
class QSystemTrayIcon;
class QTableView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void showAddDialog(const QString &prefillUrl = QString());
    void addUrls(const QStringList &urls, const QString &dir);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void applySettings();
    void updateActions();
    void onClipboardChanged();
    int currentRow() const;
    QString downloadDir() const;

    void pauseSelected();
    void resumeSelected();
    void removeSelected(bool deleteFile);
    void openSelectedFile();
    void openSelectedFolder();
    void showSettings();
    void quitApp();

    DownloadManager *m_mgr;
    QTableView *m_view;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_actAdd, *m_actPause, *m_actResume, *m_actRemove, *m_actOpen, *m_actFolder;
    QString m_clipboardCandidate;
    QString m_lastClipboard;
    bool m_quitting = false;
};
