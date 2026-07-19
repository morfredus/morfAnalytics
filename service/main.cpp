/*
 * morfAnalytics — demon de service
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

#include <morfanalytics/Service.h>
#include <morfanalytics/ModuleFactory.h>
#include <morfanalytics/Version.h>

using morfanalytics::ServiceConfig;

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString findDefaultConfig() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath("morfanalytics.json"),
        QDir(exeDir).filePath("morfanalytics.json"),
        QDir(exeDir).filePath("config/morfanalytics.json"),
#ifdef Q_OS_UNIX
        QStringLiteral("/etc/morfanalytics/morfanalytics.json"),
#endif
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

// Config de repli : un module 'analytics', pour tester le service sans config.
ServiceConfig fallbackConfig() {
    ServiceConfig c;
    morfanalytics::ModuleDef ex;
    ex.type = QStringLiteral("analytics");
    ex.id   = QStringLiteral("analytics-1");
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
    QCoreApplication::setApplicationName(QStringLiteral("morfAnalytics"));
    QCoreApplication::setApplicationVersion(morfanalytics::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("morfAnalytics — squelette de service morfSystem "
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
              << morfanalytics::ModuleFactory::knownTypes().join(", ") << '\n';
        return 0;
    }

    ServiceConfig config;
    QString configPath = parser.value(configOpt);
    if (configPath.isEmpty())
        configPath = findDefaultConfig();

    if (configPath.isEmpty()) {
        err() << "Aucune configuration trouvee : demarrage avec un module 'analytics'. "
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

    morfanalytics::Service service(config);
    for (const QString& w : service.warnings())
        err() << "Avertissement : " << w << '\n';

    if (!service.start()) {
        err() << "Le serveur HTTP n'a pas pu ecouter sur le port "
              << config.httpPort << " (deja utilise ?).\n";
        return 3;
    }

    out() << "morfAnalytics v" << morfanalytics::version() << " demarre : "
          << service.moduleCount() << " module(s), API http://"
          << config.bindAddress << ':' << service.httpPort()
          << "/  (GET /status /healthz /modules ; POST /analyze)\n";
    out().flush();

    return app.exec();
}
