/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/StatePaths.h"

#include <QDir>

namespace morfanalytics {

QString stateDir() {
    const QByteArray env = qgetenv("STATE_DIRECTORY");
    if (!env.isEmpty()) {
        const QString first = QString::fromLocal8Bit(env).split(QLatin1Char(':')).first();
        if (!first.isEmpty()) {
            QDir().mkpath(first);
            return first;
        }
    }
#if defined(Q_OS_WIN)
    const QString base = qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"));
    const QString dir  = QDir(base).filePath(QStringLiteral("morfsystem/morfanalytics/state"));
#else
    const QString dir  = QStringLiteral("/var/lib/morfsystem/morfanalytics");
#endif
    QDir().mkpath(dir);
    return dir;
}

} // namespace morfanalytics
