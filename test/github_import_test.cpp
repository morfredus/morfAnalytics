/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Verifie l'import GitHub : upsert quotidien, pas de doublon, delta jamais negatif.
 */

#include "morfanalytics/data/GitHubStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

using morfanalytics::classifyGithubAsset;
using morfanalytics::GitHubStore;

static int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static QJsonObject sampleSnap(const char* collectedAt, int viewsDay, int downloads) {
    return QJsonObject{
        {QStringLiteral("contract"), QStringLiteral("github-traffic/1")},
        {QStringLiteral("collected_at"), QString::fromUtf8(collectedAt)},
        {QStringLiteral("owner"), QStringLiteral("morfredus")},
        {QStringLiteral("repository"), QStringLiteral("morfCollector")},
        {QStringLiteral("full_name"), QStringLiteral("morfredus/morfCollector")},
        {QStringLiteral("partial"), false},
        {QStringLiteral("period"), QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("github_rolling_14d")},
            {QStringLiteral("days"), 14},
            {QStringLiteral("from"), QStringLiteral("2026-08-07")},
            {QStringLiteral("to"), QStringLiteral("2026-08-20")},
        }},
        {QStringLiteral("diagnostics"), QJsonArray{}},
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("repository"), QJsonObject{
                {QStringLiteral("stargazers_count"), 2},
                {QStringLiteral("forks_count"), 0},
                {QStringLiteral("watchers_count"), 2},
            }},
            {QStringLiteral("views"), QJsonObject{
                {QStringLiteral("count"), viewsDay},
                {QStringLiteral("uniques"), 3},
                {QStringLiteral("views"), QJsonArray{QJsonObject{
                    {QStringLiteral("timestamp"), QStringLiteral("2026-08-19T00:00:00Z")},
                    {QStringLiteral("count"), viewsDay},
                    {QStringLiteral("uniques"), 3},
                }}},
            }},
            {QStringLiteral("clones"), QJsonObject{
                {QStringLiteral("count"), 1},
                {QStringLiteral("uniques"), 1},
                {QStringLiteral("clones"), QJsonArray{}},
            }},
            {QStringLiteral("popular_paths"), QJsonArray{}},
            {QStringLiteral("referrers"), QJsonArray{}},
            {QStringLiteral("releases"), QJsonArray{QJsonObject{
                {QStringLiteral("id"), 11},
                {QStringLiteral("tag_name"), QStringLiteral("v0.7.0")},
                {QStringLiteral("name"), QStringLiteral("0.7.0")},
                {QStringLiteral("published_at"), QStringLiteral("2026-08-20T00:00:00Z")},
                {QStringLiteral("assets"), QJsonArray{QJsonObject{
                    {QStringLiteral("id"), 42},
                    {QStringLiteral("name"), QStringLiteral("morfCollector-0.7.0-win64.zip")},
                    {QStringLiteral("download_count"), downloads},
                }}},
            }}},
        }},
    };
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto win = classifyGithubAsset(QStringLiteral("SiteWatch-1.14.0-win64.zip"));
    if (win.platform != QLatin1String("windows"))
        return fail("win64 -> windows");
    const auto arm = classifyGithubAsset(QStringLiteral("svc-linux-arm64.tar.gz"));
    if (arm.architecture != QLatin1String("arm64"))
        return fail("linux-arm64");
    const auto fw = classifyGithubAsset(QStringLiteral("firmware.bin"));
    if (fw.platform != QLatin1String("firmware"))
        return fail("firmware");

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail("tmpdir");
    GitHubStore store(tmp.path() + QStringLiteral("/github.sqlite"));
    if (!store.open())
        return fail(qPrintable(store.lastError()));

    const QJsonObject a = sampleSnap("2026-08-20T02:30:00Z", 10, 5);
    if (!store.importSnapshot(QStringLiteral("obj-a"), a))
        return fail(qPrintable(store.lastError()));
    if (!store.importSnapshot(QStringLiteral("obj-a"), a))
        return fail("reimport doit rester idempotent");
    if (!store.hasObject(QStringLiteral("obj-a")))
        return fail("object_id absent");

    const QJsonObject b = sampleSnap("2026-08-21T02:30:00Z", 12, 8);
    if (!store.importSnapshot(QStringLiteral("obj-b"), b))
        return fail(qPrintable(store.lastError()));

    const QJsonObject ov = store.overview();
    if (ov.value(QStringLiteral("views_total")).toDouble() != 12)
        return fail("upsert quotidien : la journee 2026-08-19 doit rester unique");
    if (ov.value(QStringLiteral("downloads_delta")).toDouble() != 3)
        return fail("delta telechargements 8-5=3");

    const QJsonObject c = sampleSnap("2026-08-22T02:30:00Z", 12, 4);
    if (!store.importSnapshot(QStringLiteral("obj-c"), c))
        return fail(qPrintable(store.lastError()));
    const QJsonObject ov2 = store.overview();
    if (ov2.value(QStringLiteral("downloads_delta")).toDouble() != 3)
        return fail("baisse de compteur ne doit pas devenir un delta negatif");

    QTemporaryDir tmpIngest;
    if (!tmpIngest.isValid())
        return fail("tmpdir ingest");
    GitHubStore ingestStore(tmpIngest.path() + QStringLiteral("/github.sqlite"));
    if (!ingestStore.open())
        return fail(qPrintable(ingestStore.lastError()));
    const QJsonObject authority{
        {QStringLiteral("contract"), QStringLiteral("sitewatch-github/1")},
        {QStringLiteral("published_at"), QStringLiteral("2026-08-21T01:00:00Z")},
        {QStringLiteral("repositories"), QJsonArray{QJsonObject{
            {QStringLiteral("full_name"), QStringLiteral("morfredus/SiteWatch")},
            {QStringLiteral("stars"), 4},
            {QStringLiteral("views_14"), 20},
            {QStringLiteral("uniques_14"), 5},
            {QStringLiteral("clones"), 2},
            {QStringLiteral("last_release"), QStringLiteral("v1.16.0")},
            {QStringLiteral("daily"), QJsonArray{QJsonObject{
                {QStringLiteral("metric"), QStringLiteral("views")},
                {QStringLiteral("day"), QStringLiteral("2026-08-20")},
                {QStringLiteral("count"), 7},
                {QStringLiteral("uniques"), 3},
            }}},
        }}},
    };
    if (!ingestStore.ingestAuthority(authority))
        return fail(qPrintable(ingestStore.lastError()));
    const QJsonObject ovAuth = ingestStore.overview();
    if (ovAuth.value(QStringLiteral("repositories")).toInt() != 1)
        return fail("ingest SiteWatch doit creer un depot");
    if (ovAuth.value(QStringLiteral("views_total")).toDouble() != 7)
        return fail("ingest SiteWatch doit poser le trafic quotidien");
    const QJsonObject ovWindow = ingestStore.overview(
        QString(), QStringLiteral("2026-08-20"), QStringLiteral("2026-08-20"));
    if (ovWindow.value(QStringLiteral("views_total")).toDouble() != 7)
        return fail("filtre from/to");

    std::fprintf(stdout, "ok\n");
    return 0;
}
