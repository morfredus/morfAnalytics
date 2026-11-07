/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Fonctions pures : controle d'origine (CSRF) et decoupage en lots de la
 * publication morfSync. Retourne 0 si tout passe.
 */

#include <cstdio>

#include "morfanalytics/HttpGuards.h"
#include "morfanalytics/publish/MeteoSyncPublisher.h"

using namespace morfanalytics;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

int main() {
    const QByteArray same  = "POST /x HTTP/1.1\r\nHost: pi4fred:8791\r\nOrigin: http://pi4fred:8791\r\n";
    const QByteArray other = "POST /x HTTP/1.1\r\nHost: pi4fred:8791\r\nOrigin: http://evil.example\r\n";
    const QByteArray none  = "POST /x HTTP/1.1\r\nHost: pi4fred:8791\r\n";
    const QByteArray nul   = "POST /x HTTP/1.1\r\nHost: pi4fred:8791\r\norigin: null\r\n";
    const QByteArray port  = "POST /x HTTP/1.1\r\nHost: pi4fred:8791\r\nOrigin: http://pi4fred:9999\r\n";

    check(!isCrossOriginWrite("POST", same),  "meme origine : accepte");
    check(isCrossOriginWrite("POST", other),  "autre origine : refuse");
    check(!isCrossOriginWrite("POST", none),  "sans Origin (client du parc) : accepte");
    check(isCrossOriginWrite("POST", nul),    "Origin null : refuse");
    check(isCrossOriginWrite("DELETE", port), "meme hote, autre port : refuse");
    check(!isCrossOriginWrite("GET", other),  "GET : jamais controle");
    check(headerValue(nul, "Origin") == "null", "en-tete insensible a la casse");

    QHash<quint32, quint32> perDay, published;
    for (quint32 d = 20260101; d < 20260101 + 100; ++d) perDay.insert(d, 10);
    perDay.insert(0, 5);                       // cle invalide ignoree
    QVector<quint32> b = MeteoSyncPublisher::selectBatch(perDay, published, 40);
    check(b.size() == 40, "lot plafonne a 40 jours");
    check(b.first() == 20260101 && b.last() == 20260140, "ordre chronologique, plus anciens d'abord");
    for (quint32 d : b) published.insert(d, 10);
    b = MeteoSyncPublisher::selectBatch(perDay, published, 40);
    check(b.size() == 40 && b.first() == 20260141, "le lot suivant reprend apres le lot valide");
    published.insert(20260141, 10);
    perDay.insert(20260101, 11);               // revision avancee : republie
    b = MeteoSyncPublisher::selectBatch(perDay, published, 1000);
    check(b.size() == 60, "jours inchanges non republies");
    check(b.first() == 20260101, "jour dont la revision a avance est republie");

    return failures == 0 ? 0 : 1;
}
