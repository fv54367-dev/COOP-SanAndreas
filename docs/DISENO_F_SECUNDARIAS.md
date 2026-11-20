# Diseño F — secundarias coop iniciadas por cualquiera

> Diseño original (2026-10-19); implementado en F0–F2. Nada de esto se probó en juego. Rutas relativas a `coopandreas-server\`.

## 1. Cómo funcionaba
- Cada cliente corre su `main.scm`; el lanzador de MAIN arranca la misión en la PC de quien la pide. `IsOpcodeSyncable` solo manda opcodes si `CLocalPlayer::m_bIsHost` y el script está en `ms_aszSyncedScripts`; los demás reciben `OPCODE_SYNC` y lo ejecutan en un script falso (`"coopand"`). El host ignora `OPCODE_SYNC`.
- El bloque `--choice` de `coopify.py` (~50 líneas por script) usaba `Coop.IsHost()`: si la iniciaba un no-host quedaba `XXXSOLO` (individual). `EnableSyncingThisScript` agrega el nombre para siempre, por eso el individual necesita otro nombre.
- `CCommandLaunchMissionForNetworkPlayers` (1D26) lanza la misma misión individual en cada cliente: no sirve para coop. `CollectNetworkPlayersForTheMission` (1D02) devuelve los primeros 3 jugadores, sin concepto de participante.
- El server no tenía estado de misión: reenviaba según `m_bIsHost` (`scripts.cpp`, `blips.cpp`, `world.cpp`, `vehicles.cpp`); `OPCODE_SYNC` iba a todos.
- Host global único: además de misiones controla clima, tags, ENEX, blips estáticos e historia. No conviene pasarle el host al que inicia una secundaria.
- Objetivos: los blips de char/auto y los checkpoints de coordenada ya llegan por `Coop.Update*`; el "checkpoint" de red es una zona resaltada, no un blip de radar ni un aro. Ningún opcode de blip/checkpoint está en `syncedOpcodes`: los blips de coordenada llegan por el snapshot estático del host, que además borra los blips locales (bug: los de una misión individual se ven en todos y se pierden los propios). Faltan blips de objeto/pickup y aros de carrera.

## 2. Diseño: dueño de misión separado del host
El que inicia es **dueño** y su PC corre el script. El server guarda una sola misión coop activa (dueño, nombre, título, participantes). Mientras esté activa, la autoridad de script es el dueño; si no hay misión, es el host (la historia queda igual). En SCM queda un solo opcode y el bloque baja a ~5 líneas.

1. Cualquiera arranca la secundaria: el script se llama `XXXSOLO`, `$onmission = 1` y llama en bucle a `Coop.ChooseSideMissionMode('COPCAR', "Vigilante")`.
2. Individual inmediato si no hay otros jugadores, ya hay una misión coop de otro ("Ya hay una misión cooperativa en curso: <título> de <nick>") o el que la inicia está en cinemática.
3. Si no: "**Y** = cooperativa / **N** = individual (5 s)". N o timeout → individual.
4. Con Y → `COOP_MISSION_OFFER`; el server ocupa el lugar (si está libre), participantes = {dueño} y manda `COOP_MISSION_INVITE` a los demás.
5. Cada invitado ve "¿Querés unirte a la misión cooperativa <título> de <nick>? Y/N" (10 s) y responde `COOP_MISSION_REPLY`. Rechazo automático si tiene misión local, está muerto/arrestado, en cinemática o con el menú abierto.
6. El dueño ve "Esperando respuestas (n/m)". Al responder todos o a los 12 s: si aceptó al menos uno, renombra a `'COPCAR'`, hace `EnableSyncingThisScript` y el server pasa a RUNNING; si nadie aceptó, queda individual.
7. Mientras corre, solo los participantes reciben `OPCODE_SYNC`, flag de misión, blips, textos, audio, cámara, timers, teleports y objetos de misión.
8. **Abandonar:** mantener 2 durante 1,5 s. Participante: `COOP_MISSION_LEAVE`, limpieza local y `OnAMissionFlag = 0`; la misión sigue. Dueño: `CTheScripts::FailCurrentMission = 2` (como 045C) y termina para todos.
9. **Fin:** cuando el dueño ya no tiene el script sincronizado, manda `COOP_MISSION_END`; el server libera el lugar. Si el dueño se desconecta, el server cierra la misión y los participantes limpian.

### Paquetes (canal SCRIPT, al final del enum)
| Paquete | Dirección | Campos |
|---|---|---|
| `COOP_MISSION_OFFER` | C→S | `scriptName[8]`, `title[24]` |
| `COOP_MISSION_INVITE` | S→demás | `owner`, `scriptName[8]`, `title[24]`, `timeoutMs` |
| `COOP_MISSION_REPLY` | C→S | `accept` |
| `COOP_MISSION_STATE` | S→todos | `phase` (0 libre, 1 oferta, 2 en curso), `owner`, `scriptName[8]`, `title[24]`, `count`, `participants[4]`, `reason` (0 ok, 1 ocupado, 2 nadie aceptó, 3 abandonó el dueño, 4 desconexión, 5 terminó) |
| `COOP_MISSION_LEAVE` | C→S | — |
| `COOP_MISSION_END` | C→S (dueño) | `passed` |
| `MISSION_MARKERS_SNAPSHOT` (fase 3) | dueño→S→participantes | hasta 20 `{pos, sprite, colour, display, scale, friendly}` + hasta 8 aros `{pos, pointTo, radius, type}` |

### Opcode
`1D29 Coop.ChooseSideMissionMode(coopName: string8, title: string)` (condición): falso mientras se decide, verdadero al terminar. Si es coop, el C++ hace el `script_name` y `EnableSyncingThisScript`; es reentrante y guarda el estado asociado al `CRunningScript*`. Va en `sa_coop.json` + `sa_coop.db`, en las dos carpetas de SB4, y en `CCustomCommandRegistrar.h` (clase `CCommandChooseSideMissionMode`). `1D2A Coop.IsCoopMission()` ya existe.

### Cliente (`CCoopMission`)
- Estado espejo, `IsActive/IsOwner/IsParticipant/IsLocalParticipant`, **`IsScriptAuthority()`** = `IsActive() ? owner == local : m_bIsHost`.
- `Process()`: teclas Y/N de conversación (ignoradas con chat o F8 abiertos), mantener 2, detectar fin. `Draw()` en ImGui (copiar `CMinigameVs.cpp`).
- Cambiar `m_bIsHost` por `IsScriptAuthority()` **solo donde es de misión**: `COpCodeSync`, `Main.cpp` (cinemática/audio/flag), `CCutsceneSkipVote`, `CCameraMirror`, `MissionAudioHooks`, `GameHooks` (StartCutscene), `CObjectSync`, `VehicleHooks`, `CNetworkPadSync` (POOL2), comandos `*ForAll/Teleport/Locate/PutInCar/Oddjob` y los receptores de `scripts.cpp`/`blips.cpp`. Se quedan con el host: clima, tags, gangs, ENEX, blips estáticos, historia, `LaunchMissionForNetworkPlayers`, `CheatMenu`, `PedTakeHost`, `PedResetAllClaims`.
- Filtro por participante: `CollectNetworkPlayersForTheMission`, `IsNetworkPlayerActorValid` (así los bucles de los scripts dejan afuera solos al que abandonó) e iteraciones `ForAll/Teleport/Locate/PutInCar/Oddjob`.
- Blips estáticos: marcar los creados por una misión (hook de `SetCoordBlip`/`SetEntityBlip`), no mandarlos en el snapshot del mundo y no borrarlos en `Create()`. Arregla también el bug de los blips individuales.

### Server (`CCoopMission`)
- Estado y timeout de la oferta (12 s), `IsScriptAuthority`, `IsParticipant`, `SendToParticipants`, `OnPlayerLeft` (desde la desconexión).
- Handlers OFFER/REPLY/LEAVE/END: validar que el de OFFER no esté en otra misión, que solo el dueño mande END y truncar el título.
- En `scripts.cpp`, `blips.cpp`, `world.cpp` y `vehicles.cpp`: `m_bIsHost` → `IsScriptAuthority`; con misión activa, `SendToAll` → `SendToParticipants` y los `Send(target)` solo a participantes. `OPCODE_SYNC`: del dueño solo a participantes; si no, como hoy. `ENEX_SYNC`, clima, tags, blips estáticos e historia siguen con el host.

### coopify y scripts
`--choice "Nombre"` reemplaza el bloque por:
```
:COPCAR_COOP_CHOOSE
wait 0
if
  Coop.ChooseSideMissionMode('COPCAR', "Vigilante")
goto_if_false @COPCAR_COOP_CHOOSE
$NETWORK_PLAYER[0], $NETWORK_PLAYER[1], $NETWORK_PLAYER[2] = Coop.CollectNetworkPlayersForTheMission()
```
Mantiene `script_name 'XXXSOLO'` y que el bloque vaya después de `$onmission = 1`; ya no usa locales libres ni `Coop.IsHost()`; quita los `AddChatMessage` de modo. `--rechoice "Nombre"` reemplaza el bloque viejo en los 22 scripts (match exacto, CRLF) y falla si no coincide. Cada script baja ~350–450 bytes; no se toca MAIN.

## 3. Secundarias convertidas (22) y objetivos
| Script | Misión | Falta (fase 3) |
|---|---|---|
| AMBULAN | Paramédico | — |
| BCOUR | Mensajero | 12 aros |
| BLOOD | Blood Ring | 2 aros + `SetCoords` |
| BOAT | Escuela de botes | 190 `Blip.AddForObject` (boyas) |
| BSKOOL | Escuela de motos | sin blips (conos locales) |
| BURGJB | Robo | — |
| COPCAR | Vigilante | revisar 12 coordenadas sin conversión |
| CPRACE | Carreras | 7 aros |
| DESERT5 | Chiliad | 15 aros |
| DSKOOL | Escuela de manejo | 2 aros |
| FIRETRU | Bombero | — |
| FREIGHT | Tren | — |
| KICKSTA | Kickstart | — |
| MTBIKER | Mountain bike | 6 aros |
| PIMP | Proxeneta | — |
| POOL2 | Pool VS | pasar "primer jugador de red" a "primer participante" |
| QUARRY | Cantera | 8 blips de objeto + 11 aros |
| SHRANGE | Ammu-Nation | — |
| STUNT | Stunt BMX/NRG | 1 aro |
| TAXIODD | Taxi | 9 `Blip.AddSpriteForCoord(63,…)` |
| TRIA | Triatlón | 10 aros |
| TRUCK | Camionero | — |

Los blips de char/auto y los checkpoints de coordenada están completos. `MISSION_MARKERS_SNAPSHOT` resuelve en C++ (sin tocar scripts) los aros de 9 scripts, los blips de objeto de BOAT/QUARRY, los sprites de TAXIODD y el radar del dueño no-host: el dueño recorre `CRadar::ms_RadarTrace` y `ScriptCheckpointArray` cada 250 ms, manda solo si cambió y el participante replica/borra.

## 4. Riesgos
- Una sola misión coop: si el host arranca historia mientras corre una secundaria coop, se juega individual (el C++ avisa). Bloquear el arranque obligaría a tocar MAIN: descartado.
- Los peds/autos de misión los ven todos, también los no participantes (la sync de entidades es del mundo). Filtrar entidades es otro proyecto.
- ~30 `m_bIsHost` en el cliente y ~25 en el server: probar dueño host + invitado y dueño no-host + host invitado.
- No se puede unir a una misión ya empezada (el snapshot de fase 3 lo habilitaría).
- `FailCurrentMission` corre la rama M_FAIL: verificar POOL2 y las escuelas (BSKOOL, DSKOOL, BOAT).
- `ms_aszSyncedScripts` nunca se vacía: no renombrar `XXXSOLO` a mano.
- Recompensas: solo COPCAR usa `GrantOddjobRewardToNetworkPlayers`; el resto queda en el dueño (el dinero es de cada uno).
- Y/N usan "Conversación sí/no"; ignorarlas con chat/F8 abiertos. Mantener 2 no choca porque el participante tiene `$onmission = 1`.

## 5. Orden
1. Fase 0: marcar blips de misión (independiente).
2. Fase 1: server (estado, paquetes, filtros), cliente (`CCoopMission`, opcode, reemplazos), SCM (`--choice`/`--rechoice`).
3. Prueba con 2 instancias: no-host inicia y el host acepta; host inicia y el invitado rechaza; invitado abandona; dueño abandona; dueño se desconecta.
4. Fase 2: pulir abandono del dueño (POOL2 y escuelas).
5. Fase 3: `MISSION_MARKERS_SNAPSHOT` y validar BOAT, QUARRY, CPRACE, TRIA y DESERT5.
