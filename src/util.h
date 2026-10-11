#pragma once
#include <QString>
#include <QtGlobal>

namespace util {

inline QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("?");
    const char *units[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    double v = double(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return QString::number(v, 'f', u == 0 ? 0 : (v < 10 ? 2 : 1)) + QLatin1Char(' ')
           + QString::fromUtf8(units[u]);
}

inline QString humanSpeed(qint64 bytesPerSec)
{
    return humanSize(bytesPerSec) + QStringLiteral("/с");
}

inline QString humanTime(qint64 seconds)
{
    if (seconds < 0)
        return QString();
    qint64 h = seconds / 3600;
    qint64 m = (seconds % 3600) / 60;
    qint64 s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1 ч %2 мин").arg(h).arg(m);
    if (m > 0)
        return QStringLiteral("%1 мин %2 с").arg(m).arg(s);
    return QStringLiteral("%1 с").arg(s);
}

} // namespace util
