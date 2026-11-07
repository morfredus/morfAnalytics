/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QByteArray>
#include <QList>

namespace morfanalytics {

// Valeur d'un en-tete HTTP (nom insensible a la casse) dans un bloc d'en-tetes
// brut, vide s'il est absent.
inline QByteArray headerValue(const QByteArray& headerBlock, const QByteArray& name) {
    const QByteArray prefix = name.toLower() + ':';
    for (const QByteArray& line : headerBlock.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.toLower().startsWith(prefix))
            return l.mid(prefix.size()).trimmed();
    }
    return {};
}

// Protection CSRF des ecritures (POST, PUT, PATCH, DELETE) : une page web visitee
// depuis le LAN peut poster vers ce service avec le navigateur de l'utilisateur.
// Un navigateur envoie TOUJOURS `Origin` sur une ecriture cross-origin ; les clients
// du parc (SiteWatch, morfMonitor, scripts, curl) n'en envoient pas et passent.
// Rend true si l'ecriture doit etre refusee : `Origin` present et son host:port
// different de l'en-tete `Host`.
inline bool isCrossOriginWrite(const QByteArray& method, const QByteArray& headerBlock) {
    if (method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE")
        return false;
    QByteArray origin = headerValue(headerBlock, "Origin");
    if (origin.isEmpty())
        return false;
    const int scheme = origin.indexOf("://");
    if (scheme >= 0)
        origin = origin.mid(scheme + 3);
    while (origin.endsWith('/'))
        origin.chop(1);
    const QByteArray host = headerValue(headerBlock, "Host");
    return origin.toLower() != host.toLower();   // "null" ou absent de Host => refus
}

} // namespace morfanalytics
