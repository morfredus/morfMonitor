/*
 * morfMonitor — demon de service
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Charge une configuration JSON, demarre les modules, ouvre l'API HTTP et
 * annonce sa presence sur le LAN (morfBeacon). Squelette reutilisable : le
 * comportement propre au service vit dans les modules (voir IModule).
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextStream>

#include <morfmonitor/Service.h>
#include <morfmonitor/ModuleFactory.h>
#include <morfmonitor/Version.h>

using morfmonitor::ServiceConfig;

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString findDefaultConfig() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath("morfmonitor.json"),
        QDir(exeDir).filePath("morfmonitor.json"),
        QDir(exeDir).filePath("config/morfmonitor.json"),
#ifdef Q_OS_UNIX
        QStringLiteral("/etc/morfmonitor/morfmonitor.json"),
#endif
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

// Config de repli : un module 'example', pour tester le squelette sans config.
ServiceConfig fallbackConfig() {
    ServiceConfig c;
    morfmonitor::ModuleDef ex;
    ex.type = QStringLiteral("example");
    ex.id   = QStringLiteral("example-1");
    c.modules.push_back(ex);
    return c;
}

bool loadConfig(const QString& path, ServiceConfig* outCfg, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("impossible d'ouvrir %1 : %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("JSON invalide dans %1 : %2").arg(path, pe.errorString());
        return false;
    }
    *outCfg = ServiceConfig::fromJson(doc.object());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("morfMonitor"));
    QCoreApplication::setApplicationVersion(morfmonitor::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("morfMonitor — squelette de service morfSystem "
                       "(API HTTP + annonce LAN, modules enfichables)."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOpt({"c", "config"},
        QStringLiteral("Fichier de configuration JSON."), QStringLiteral("chemin"));
    QCommandLineOption listOpt("list-types",
        QStringLiteral("Liste les types de modules disponibles puis quitte."));
    parser.addOption(configOpt);
    parser.addOption(listOpt);
    parser.process(app);

    if (parser.isSet(listOpt)) {
        out() << "Types de modules disponibles : "
              << morfmonitor::ModuleFactory::knownTypes().join(", ") << '\n';
        return 0;
    }

    ServiceConfig config;
    QString configPath = parser.value(configOpt);
    if (configPath.isEmpty())
        configPath = findDefaultConfig();

    if (configPath.isEmpty()) {
        err() << "Aucune configuration trouvee : demarrage avec un module 'example'. "
                 "Fournir --config pour votre configuration.\n";
        config = fallbackConfig();
    } else {
        QString error;
        if (!loadConfig(configPath, &config, &error)) {
            err() << "Erreur de configuration : " << error << '\n';
            return 2;
        }
        out() << "Configuration chargee : " << configPath << '\n';
    }

    morfmonitor::Service service(config);
    for (const QString& w : service.warnings())
        err() << "Avertissement : " << w << '\n';

    if (!service.start()) {
        err() << "Le serveur HTTP n'a pas pu ecouter sur le port "
              << config.httpPort << " (deja utilise ?).\n";
        return 3;
    }

    out() << "morfMonitor v" << morfmonitor::version() << " demarre : "
          << service.moduleCount() << " module(s), API http://"
          << config.bindAddress << ':' << service.httpPort()
          << "/  (GET /status /healthz /modules ; POST /example)\n";
    out().flush();

    return app.exec();
}
