#pragma once

// B3: reemplazo minimo del stdafx.h del server para el testbot. shared/network/eNetworkEntityType.h incluye
// "stdafx.h" (en el server resuelve a server/src/stdafx.h, que arrastra todo el server); aca solo hacen falta
// las cabeceras genericas que usan los paquetes compartidos.
//
// OJO: este header tiene que incluirse antes que TestbotPackets.h y NO debe incluir network/packet.h
// (PacketRegistrar necesita ver GetPacketFactory antes).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#include "enet/enet.h"

#include "config.h"
#include "semver.h"
