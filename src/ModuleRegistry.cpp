/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/ModuleRegistry.h"
#include "morfanalytics/IModule.h"

#include <QDebug>

namespace morfanalytics {

ModuleRegistry::ModuleRegistry(QObject* parent) : QObject(parent) {}
ModuleRegistry::~ModuleRegistry() = default;

void ModuleRegistry::add(IModule* module) {
    if (!module)
        return;
    module->setParent(this);
    m_modules.push_back(module);
    connect(module, &IModule::updated, this, &ModuleRegistry::updated);
}

void ModuleRegistry::startAll() {
    // Le retour de start() etait ignore : un module en echec restait compte
    // dans « N module(s) » et le service paraissait sain. Un module qui ne
    // demarre pas est une information de premier ordre — elle va au journal.
    for (IModule* m : m_modules) {
        if (!m->start())
            qCritical().noquote() << QStringLiteral(
                "module '%1' : demarrage EN ECHEC — le service tourne mais ce "
                "module ne fera rien (voir les messages precedents).").arg(m->id());
    }
}
void ModuleRegistry::stopAll()  { for (IModule* m : m_modules) m->stop(); }
int  ModuleRegistry::count() const { return m_modules.size(); }

QJsonArray ModuleRegistry::modulesJson() const {
    QJsonArray arr;
    for (const IModule* m : m_modules) {
        QJsonObject o;
        o["id"]     = m->id();
        o["type"]   = m->type();
        o["status"] = m->statusJson();
        arr.append(o);
    }
    return arr;
}

IModule* ModuleRegistry::firstOfType(const QString& type) const {
    for (IModule* m : m_modules) {
        if (m->type() == type)
            return m;
    }
    return nullptr;
}

QJsonObject ModuleRegistry::moduleJson(const QString& id, bool* found) const {
    for (const IModule* m : m_modules) {
        if (m->id() == id) {
            if (found) *found = true;
            QJsonObject o;
            o["id"]     = m->id();
            o["type"]   = m->type();
            o["status"] = m->statusJson();
            return o;
        }
    }
    if (found) *found = false;
    return QJsonObject{};
}

QJsonObject ModuleRegistry::metrics() const {
    // >>> A ENRICHIR : exposez ici les compteurs pertinents pour votre service.
    QJsonObject m;
    m["modules"] = m_modules.size();
    return m;
}

QString ModuleRegistry::state() const {
    if (m_modules.isEmpty())
        return QStringLiteral("starting");
    return QStringLiteral("ok");
}

} // namespace morfanalytics
