/*
 * morfMonitor - test du controle d'origine (garde CSRF)
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/OriginCheck.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "ECHEC ligne %d : %s\n", __LINE__, #cond); \
                                        std::exit(1); } } while (0)

using morfmonitor::isCrossOriginWrite;
using morfmonitor::allowsCors;

int main() {
    const QByteArray same  = "POST /x HTTP/1.1\r\nHost: pi4dev:8790\r\nOrigin: http://pi4dev:8790";
    const QByteArray other = "POST /x HTTP/1.1\r\nHost: pi4dev:8790\r\nOrigin: http://evil.example";
    const QByteArray none  = "POST /x HTTP/1.1\r\nHost: pi4dev:8790";
    const QByteArray nul   = "POST /x HTTP/1.1\r\nHost: pi4dev:8790\r\nOrigin: null";

    CHECK(!isCrossOriginWrite("POST", same));     // meme origine : accepte
    CHECK(!isCrossOriginWrite("POST", none));     // client non-navigateur : accepte
    CHECK(isCrossOriginWrite("POST", other));     // autre origine : refuse
    CHECK(isCrossOriginWrite("DELETE", other));
    CHECK(isCrossOriginWrite("POST", nul));       // origine opaque : refuse
    CHECK(!isCrossOriginWrite("GET", other));     // lecture : jamais concernee
    CHECK(!isCrossOriginWrite("POST", "POST / HTTP/1.1\r\nhost: A:1\r\norigin: http://a:1"));  // casse
    CHECK(allowsCors("GET") && allowsCors("HEAD"));
    CHECK(!allowsCors("POST") && !allowsCors("DELETE"));
    std::puts("origin-check : OK");
    return 0;
}
