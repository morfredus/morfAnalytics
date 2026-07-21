/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/Service.h"
#include "morfanalytics/ModuleRegistry.h"
#include "morfanalytics/HttpServer.h"
#include "morfanalytics/ModuleFactory.h"
#include "morfanalytics/IModule.h"
#include "morfanalytics/Version.h"

#include "morfbeacon/Heartbeat.h"
#include "morfbeacon/PresenceConfig.h"

#include <utility>

namespace morfanalytics {

Service::Service(ServiceConfig config, QObject* parent)
    : QObject(parent),
      m_config(std::move(config)),
      m_registry(new ModuleRegistry(this)),
      m_http(nullptr) {

    // Construit les modules declares. Une erreur sur l'un (type inconnu,
    // parametre manquant) n'empeche pas les autres : on la note.
    for (const ModuleDef& def : m_config.modules) {
        QString error;
        IModule* m = ModuleFactory::create(def, &error);
        if (!m) {
            m_warnings << error;
            continue;
        }
        m_registry->add(m);
    }

    m_http = new HttpServer(m_config, m_registry, this);
}

Service::~Service() = default;

bool Service::start() {
    m_registry->startAll();
    const bool httpOk = (m_config.httpPort == 0) ? true : m_http->start();

    if (m_config.beaconEnabled) {
        morfbeacon::PresenceConfig pc;
        pc.appName             = m_config.appName;
        pc.version             = morfanalytics::version();
        pc.instanceId          = m_config.instanceId;
        // Capacite annoncee : c'est par elle que MeteoHub reconnait un service
        // d'analyse, et non par son nom — que l'utilisateur peut changer.
        // Renommer l'application n'interrompt donc pas l'integration.
        pc.capabilities        = {QStringLiteral("advanced_analysis")};

        // morfAnalytics sert une page d'accueil : il la declare, et « web_ui »
        // s'ajoute automatiquement aux capacites emises. Un observateur peut
        // alors proposer un lien vers les analyses sans rien connaitre de
        // morfAnalytics — c'est deja ainsi que MeteoHub le detecte, par capacite
        // et jamais par nom.
        pc.webUiPath        = QStringLiteral("/");
        pc.webUiLabel       = QStringLiteral("Analyses");
        pc.webUiDescription = QStringLiteral(
            "Statistiques longue periode et correlations sur l'historique des equipements.");
        pc.udpPort             = m_config.beaconUdpPort;
        pc.broadcastIntervalMs = m_config.beaconIntervalMs;
        pc.statusPort          = m_http ? m_http->port() : 0;
        pc.statusBindAddress   = m_config.bindAddress;

        m_heartbeat = new morfbeacon::Heartbeat(pc, m_registry, this);
        m_heartbeat->start();
    }

    return httpOk;
}

void Service::stop() {
    if (m_heartbeat)
        m_heartbeat->stop();
    if (m_http)
        m_http->stop();
    m_registry->stopAll();
}

int Service::moduleCount() const   { return m_registry->count(); }
quint16 Service::httpPort() const  { return m_http ? m_http->port() : 0; }
QStringList Service::warnings() const { return m_warnings; }
ModuleRegistry* Service::registry() const { return m_registry; }

} // namespace morfanalytics
