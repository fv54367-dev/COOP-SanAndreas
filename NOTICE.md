# NOTICE — Origen y créditos / Origin and credits

Este proyecto es un **fork comunitario no oficial** de
**[CoopAndreas](https://github.com/Tornamic/CoopAndreas)**, creado por **Tornamic** y sus colaboradores
(licencia GPL-3.0). Todo lo que existe aquí fue posible gracias a esa base: la infraestructura de red,
el sistema de opcodes `Coop.*`, la traducción de los scripts de misión y las primeras misiones cooperativas
son trabajo original suyo.

This project is an **unofficial community fork** of CoopAndreas by Tornamic and contributors (GPL-3.0).
It is not affiliated with, endorsed by, or a replacement for the original project.

## Licencia / License

- Este fork se distribuye bajo **GNU GPL v3** (ver `LICENSE`), igual que el proyecto original.
- Se conservan los avisos de copyright originales. El código fuente de todas las modificaciones se publica bajo la misma licencia.
- Los componentes de `third_party/` mantienen sus propias licencias (enet, imgui, plugin-sdk, INIReader, utf8, DiscordRPC SDK, etc.).

## Cambios respecto al original / Changes from upstream

Ver el historial de commits (`git log`) y `CoopAndreas_ESTADO.md`. Resumen: misiones adicionales coopificadas
(Drive-Thru, Nines And AK's, Drive-By, Vigilante), sincronización de vida máxima, opcode
`Coop.GrantOddjobRewardToNetworkPlayers`, y mejoras de estabilidad/sincronización en curso.
Los reportes automáticos de crashes están **desactivados por defecto** en este fork (no se envían datos al servidor del proyecto original).

## Rockstar Games / Take-Two

Mod no oficial para *Grand Theft Auto: San Andreas*. Requiere una copia legítima del juego.
**No incluye ni distribuye archivos de Rockstar Games.** No está afiliado a Rockstar Games ni a Take-Two Interactive.
Proyecto sin fines comerciales; como máximo se aceptan donaciones voluntarias que no dan ventajas dentro del juego.

## Marca / Name

"CoopAndreas" es el nombre del proyecto original. Este fork usará un nombre propio para no inducir a confusión.

## Código portado de project_gta_sa_coop (GPL-3.0)

La capa "mission mirror" (`client/src/CMissionMirror.*`, `CMirrorPolicy.h` y las que se vayan agregando: blips, áreas, HUD) está portada y adaptada de `sametozbalkan/project_gta_sa_coop` (GPL-3.0), diseño en su `docs/design/mirror-layer.md`. Adaptaciones: el "host" es `CCoopMission::IsScriptAuthority()` y las recompensas (dinero/respeto) no se espejan.

Paquete M2 (blips de la misión): `client/src/CMirrorBlips.*`, `client/src/CMirrorBlipsPolicy.h`, `client/src/PacketHandlers/mirror_blips.cpp`, `server/src/PacketHandlers/mirror_blips.cpp` y `shared/network/packets/mirror_blips.h`, portados de sus homónimos en `project_gta_sa_coop` y adaptados: ids de red de 64 bits (`pedid_t`/`vehicleid_t`/`playerid_t`), reenvío del server con `CCoopMission::Relay`, sin objetos ni trenes por id (no hay `CMirrorObjects`/`CTrainSync`: siguen la posición de la autoridad), eventos de sesión sondeados por frame en vez de `CSessionLifecycle`, interruptor `mirror-blips`.

## Código adaptado de sa-coop (Karimov-Akbar, MIT)

`client/src/CHighStakesRace.cpp`: lectura de ruta, progreso y ranking de CPRACE adaptados de `src/client/features/story/race/Race.cpp`, commit `4135fc5`; https://github.com/Karimov-Akbar/sa-coop. Se aplica el aviso MIT conservado abajo.

`client/src/CCoopLowrider.cpp`: concurso por equipos, juicio, animaciones y cámaras de `src/client/features/story/minigames/Minigames.cpp`; constructor de opcodes de `src/client/shared/game/Script.*`. Origen: https://github.com/Karimov-Akbar/sa-coop. Adaptación al protocolo y participantes de CoopAndreas.

`client/src/CCoopPool.cpp`: adaptación de `src/client/features/story/pool/Pool.cpp` (modo nativo POOL2 con segundo pad remoto, cámara, objetos y pose). Mismo origen y licencia MIT; adaptado a sesiones independientes, consentimiento F7, máximo dos jugadores y partidas sin apuesta.

`client/src/CActivityChallenge.cpp`: adapted from `src/client/features/story/contest/Contest.cpp` (sa-coop, Karimov-Akbar): native activity detection, score/time metrics and basketball entry. Adapted to directed two-player consent, independent rewards, server-validated sessions and existing race VS.

`client/src/CScenePartner.cpp`, `CSceneMocap.cpp`, `CSceneGame.cpp`, `CSceneScript.cpp` y `SceneCompat/`: portados y adaptados de `features/story/cutscene-partner/ScenePartner.*`, `cutscene-clips/SceneMocap.*` y `shared/game/{Game,Types,Anim,Script}.*` de Karimov-Akbar/sa-coop. Incluye sus tablas `cutscenes.txt` y `cutscene_ways.txt`; no incluye datos de animación de Rockstar. Autoridad de misión, confirmación de escena, protocolo y limpieza propios.

`client/src/CMissionMachines.cpp`, `client/src/CMissionMachines.h`: puesto de máquinas para el invitado (torreta y RC) de `src/client/features/story/rc-toys/RcToys.cpp` de Karimov-Akbar/sa-coop (MIT). Mismo origen https://github.com/Karimov-Akbar/sa-coop. Adaptado: dueño de misión cualquiera (no sólo host), puestos por cantidad de invitados, y el daño del invitado lo aplica el dueño con el CVehicle::InflictDamage del propio juego (gta-reversed Vehicle.cpp) para que el script cuente nativo.

`client/src/CMissionObjectMoves.cpp`, `client/src/CMissionObjectMoves.h` (T70): canal de objeto movido por el invitado; tecnica de objeto compartido con dueno unico de `src/client/features/mission-objects/Objects.cpp` de Karimov-Akbar/sa-coop (MIT), mismo origen. El dueno aplica la posicion a su objeto real y su propio script cuenta (contactos/locates nativos), sin tareas de transporte.

`client/src/CMissionWarTable.cpp`, `client/src/CMissionWarTable.h` (T75): el invitado en la mesa de guerra de New Model Army (CJ al interior area 10, cámara y espera del piso al volver) de `features/story/rc-toys/RcToys.cpp`, y el imán del compañero (botón de fuego, objeto 1 m bajo el RC, ATTACH_OBJECT_TO_CAR como el script) de `features/mission-objects/Objects.cpp`, ambos de Karimov-Akbar/sa-coop (MIT), mismo origen. Adaptado: el objeto real lo mueve el canal de T70 (no se adjunta al RC remoto) y cae al soltarlo con el bit `released`.

`client/src/CGpsRoute.cpp`, `client/src/CGpsRoute.h`: ruta GPS por calles de `src/client/features/waypoint/Gps.cpp` de Karimov-Akbar/sa-coop (MIT). Mismo origen https://github.com/Karimov-Akbar/sa-coop. Adaptado: sólo el punto propio del mapa (sin compartirlo por protocolo; los puntos remotos ya existían en CoopAndreas), sólo con sesión activa e interruptor en el menú F1.

`client/src/CSocialActions.cpp`, `client/src/CEmoteIfp.h`: consentimiento de teletransporte de `src/client/features/teleport/` y animaciones de a dos y lectura de IFP de `src/client/features/anims/{Emotes,Anims,Ifp}.cpp` de Karimov-Akbar/sa-coop (MIT). Mismo origen https://github.com/Karimov-Akbar/sa-coop. Adaptado a sesiones validadas por el server, coordenadas del server y animaciones del propio juego; sin animaciones de terceros.

T59: client/src/CVoiceAdpcm.h portado de src/client/features/voice/Adpcm.h. CVoice.cpp adapta el diseño WinMM sin callbacks y colas acotadas de Voice.cpp. https://github.com/Karimov-Akbar/sa-coop
T63: `client/src/CMissionGuestCounts.cpp` adapta `Ask`, `Answer` y una selección conservadora de `kSpots` de `src/client/features/story/mission-rules/MissionRules.cpp` (Karimov-Akbar/sa-coop, MIT, mismo origen indicado arriba). Autoridad de misión, reconsultas aisladas, participantes y protocolo propios.

T65: `client/src/CMissionPhotos.cpp` adapta la transmisión de marcas de fotografías de `src/client/features/mission-objects/Objects.cpp` (Karimov-Akbar/sa-coop, MIT, mismo origen). Sesión por intento, autoridad de misión, participantes, identidad completa y preservación de flags propios adaptados a CoopAndreas.

T66: `client/src/CMissionGuestActions.cpp` adapta `kSpots`, tramos de colocación y `Throw`/`HostThrow` de `src/client/features/story/mission-rules/MissionRules.cpp` (Karimov-Akbar/sa-coop, MIT). Secuencias dirigidas por actor, arbitraje, autoridad de misión, sesiones y limpieza propios.

MIT License

Copyright (c) 2026 Karimov-Akbar

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

T69: CHomeInvasion.cpp adapta la balaclava temporal y UpdateOwnNoise de
`src/client/features/story/missions/Missions.cpp`, Karimov-Akbar/sa-coop (MIT,
copyright y licencia completos arriba). El protocolo por intento, filtrado por
participantes, protección del guardado y convivencia del HUD son propios.
T61: server/src/CInternet.* y shared/ConnectionCode.h adaptan UPnP IGD (SSDP/SOAP) y el codec base32 de src/common/api/nat-traversal/NatTraversal.{cpp,h} de Karimov-Akbar/sa-coop (MIT, licencia arriba). Sin dependencias nuevas; sin STUN, senalizacion externa ni TURN.

T71: CHomeInvasion.cpp adapta Held, TakeUp, HandleHold, FollowHolder y PartnerHolds
de `src/client/features/mission-objects/Objects.cpp`, y CrateAnswer/kGunsTruck de
`src/client/features/story/missions/Missions.cpp`, Karimov-Akbar/sa-coop (MIT,
copyright y licencia completos arriba). Arbitraje previo, época y CObjectSync
adaptados a la autoridad de misión de CoopAndreas.

T90: client/src/CGarageSync.* adapta los tipos de garaje de script (ScriptDoor/DoorShared) y el manejo del estado de
puerta de `src/client/features/garages/Garages.cpp`, Karimov-Akbar/sa-coop (MIT, copyright y licencia completos
arriba). Paquete GARAGE_DOOR, orden por servidor, reenvío a quien entra tarde y hash de nombre son propios.
