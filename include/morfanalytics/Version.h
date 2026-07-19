/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace morfanalytics {

// Version, injectee par CMake depuis le fichier VERSION.
#ifndef MORFANALYTICS_VERSION
#  define MORFANALYTICS_VERSION "dev"
#endif

inline QString version() { return QStringLiteral(MORFANALYTICS_VERSION); }

// Version du protocole HTTP/JSON expose. >>> A ADAPTER si l'API change. <<<
inline constexpr const char* kProtocol = "morfanalytics/1";

} // namespace morfanalytics
