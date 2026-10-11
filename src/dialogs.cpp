#include "dialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

AddUrlDialog::AddUrlDialog(const QString &defaultDir, const QString &prefillUrl, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Добавить загрузку"));
    resize(560, 280);

    m_urls = new QPlainTextEdit(this);
    m_urls->setPlaceholderText(QStringLiteral("Одна ссылка на строку (http, https, ftp)"));
    m_urls->setPlainText(prefillUrl);

    m_dir = new QLineEdit(defaultDir, this);
    auto *browseBtn = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &AddUrlDialog::browse);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browseBtn);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Скачать"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral("Ссылки:"), this));
    lay->addWidget(m_urls, 1);
    lay->addWidget(new QLabel(QStringLiteral("Сохранить в:"), this));
    lay->addLayout(dirRow);
    lay->addWidget(buttons);
}

QStringList AddUrlDialog::urls() const
{
    QStringList out;
    const QStringList lines = m_urls->toPlainText().split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        const QUrl u(t);
        if (u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")
                            || u.scheme() == QLatin1String("ftp")))
            out << t;
    }
    return out;
}

QString AddUrlDialog::directory() const
{
    return m_dir->text().trimmed();
}

void AddUrlDialog::browse()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для загрузок"),
                                                        m_dir->text());
    if (!d.isEmpty())
        m_dir->setText(d);
}

// ---------------------------------------------------------------- settings

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Настройки"));
    QSettings s;

    const QString defDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_dir = new QLineEdit(s.value(QStringLiteral("downloadDir"), defDir).toString(), this);
    auto *browseBtn = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browse);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browseBtn);

    m_concurrent = new QSpinBox(this);
    m_concurrent->setRange(1, 20);
    m_concurrent->setValue(s.value(QStringLiteral("maxConcurrent"), 3).toInt());

    m_segments = new QSpinBox(this);
    m_segments->setRange(1, 16);
    m_segments->setValue(s.value(QStringLiteral("maxSegments"), 8).toInt());

    m_clipboard = new QCheckBox(QStringLiteral("Предлагать скачать ссылки из буфера обмена"), this);
    m_clipboard->setChecked(s.value(QStringLiteral("watchClipboard"), true).toBool());

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Папка по умолчанию:"), dirRow);
    form->addRow(QStringLiteral("Одновременных загрузок:"), m_concurrent);
    form->addRow(QStringLiteral("Потоков на файл:"), m_segments);
    form->addRow(m_clipboard);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);
    resize(520, sizeHint().height());
}

void SettingsDialog::browse()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для загрузок"),
                                                        m_dir->text());
    if (!d.isEmpty())
        m_dir->setText(d);
}

void SettingsDialog::accept()
{
    QSettings s;
    s.setValue(QStringLiteral("downloadDir"), m_dir->text().trimmed());
    s.setValue(QStringLiteral("maxConcurrent"), m_concurrent->value());
    s.setValue(QStringLiteral("maxSegments"), m_segments->value());
    s.setValue(QStringLiteral("watchClipboard"), m_clipboard->isChecked());
    QDialog::accept();
}
