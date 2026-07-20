/*
 * morfMonitor — exemple de demonstration
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Demarre le service avec un module 'example', puis expose l'API. A tester :
 *   curl http://localhost:8799/status
 *   curl http://localhost:8799/modules
 *   curl -X POST http://localhost:8799/example -d '{"hello":"world"}'
 */

#include <QCoreApplication>

#include <morfmonitor/Service.h>
#include <morfmonitor/ServiceConfig.h>
#include <morfmonitor/Version.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    morfmonitor::ServiceConfig cfg;
    cfg.httpPort         = 8799;
    cfg.beaconIntervalMs = 5000;

    morfmonitor::ModuleDef ex;
    ex.type   = QStringLiteral("example");
    ex.id     = QStringLiteral("example-demo");
    ex.params = QJsonObject{ {"type", "example"}, {"period_ms", 3000} };
    cfg.modules.push_back(ex);

    morfmonitor::Service service(cfg);
    if (!service.start()) {
        qWarning("API HTTP non demarree (port %u occupe ?)", cfg.httpPort);
        return 1;
    }

    qInfo("morfMonitor demo v%s : %d module(s) ; GET http://localhost:%u/status",
          qUtf8Printable(morfmonitor::version()), service.moduleCount(), service.httpPort());

    return app.exec();
}
