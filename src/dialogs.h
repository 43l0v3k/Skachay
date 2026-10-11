#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QCheckBox;

class AddUrlDialog : public QDialog
{
    Q_OBJECT
public:
    AddUrlDialog(const QString &defaultDir, const QString &prefillUrl, QWidget *parent = nullptr);
    QStringList urls() const;
    QString directory() const;

private:
    void browse();
    QPlainTextEdit *m_urls;
    QLineEdit *m_dir;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void browse();
    void accept() override;

    QLineEdit *m_dir;
    QSpinBox *m_concurrent;
    QSpinBox *m_segments;
    QCheckBox *m_clipboard;
};
