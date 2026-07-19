/*
 * morfAnalytics — exemple de demonstration
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Demarre le service avec un module 'example', puis expose l'API. A tester :
 *   curl http://localhost:8799/status
 *   curl http://localhost:8799/modules
 *   curl -X POST http://localhost:8799/example -d '{"hello":"world"}'
 */

#include <QCoreApplication>

#include <morfanalytics/Service.h>
#include <morfanalytics/ServiceConfig.h>
#include <morfanalytics/Version.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    morfanalytics::ServiceConfig cfg;
    cfg.httpPort         = 8799;
    cfg.beaconIntervalMs = 5000;

    morfanalytics::ModuleDef ex;
    ex.type   = QStringLiteral("analytics");
    ex.id     = QStringLiteral("analytics-demo");
    ex.params = QJsonObject{ {"maintenance_ms", 5000} };
    cfg.modules.push_back(ex);

    morfanalytics::Service service(cfg);
    if (!service.start()) {
        qWarning("API HTTP non demarree (port %u occupe ?)", cfg.httpPort);
        return 1;
    }

    qInfo("morfAnalytics demo v%s : %d module(s) ; GET http://localhost:%u/status",
          qUtf8Printable(morfanalytics::version()), service.moduleCount(), service.httpPort());

    return app.exec();
}
