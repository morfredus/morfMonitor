/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace morfmonitor {

// Version, injectee par CMake depuis le fichier VERSION.
#ifndef MORFMONITOR_VERSION
#  define MORFMONITOR_VERSION "dev"
#endif

inline QString version() { return QStringLiteral(MORFMONITOR_VERSION); }

// Version du protocole HTTP/JSON expose. >>> A ADAPTER si l'API change. <<<
inline constexpr const char* kProtocol = "morfmonitor/1";

} // namespace morfmonitor
