/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QByteArray>
#include <QList>

namespace morfmonitor {

// Valeur d'un en-tete HTTP (nom insensible a la casse), vide si absent.
inline QByteArray headerValue(const QByteArray& headerBlock, const QByteArray& nameLower) {
    for (const QByteArray& line : headerBlock.split('\n')) {
        const QByteArray l = line.trimmed();
        const int colon = l.indexOf(':');
        if (colon > 0 && l.left(colon).trimmed().toLower() == nameLower)
            return l.mid(colon + 1).trimmed();
    }
    return {};
}

// Garde CSRF (spec commune du parc) : une ECRITURE (POST, PUT, PATCH, DELETE) dont l'en-tete
// Origin est present et designe un autre host:port que l'en-tete Host vient d'une page web
// tierce visitee depuis le LAN. Les navigateurs envoient toujours Origin sur une ecriture
// cross-origin ; les clients non-navigateur (autres services du parc, curl, scripts)
// n'en envoient pas et passent. Origin: null (page sandboxee) est refuse aussi.
inline bool isCrossOriginWrite(const QByteArray& method, const QByteArray& headerBlock) {
    if (method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE")
        return false;
    QByteArray origin = headerValue(headerBlock, "origin");
    if (origin.isEmpty())
        return false;
    const int scheme = origin.indexOf("://");
    if (scheme >= 0)
        origin = origin.mid(scheme + 3);
    return origin.toLower() != headerValue(headerBlock, "host").toLower();
}

// Les ecritures ne portent jamais Access-Control-Allow-Origin: * (aucune autorisation cross-origin).
inline bool allowsCors(const QByteArray& method) {
    return method == "GET" || method == "HEAD";
}

} // namespace morfmonitor
