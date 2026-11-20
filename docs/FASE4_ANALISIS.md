# Fase 4/5: análisis de los `Coop.*` en los scripts (2026-10-25)

Análisis de solo lectura de `coopandreas-server/scm/scripts/*.txt` (303 archivos; 131 con `Coop.*`) y de
`client/src/Commands/Commands/*.cpp`, para pasar a C++ la **decisión** de cuándo pasa cada cosa y poder volver a los
scripts originales (ver `docs/PLAN_CPP.md`). Ya hechos y fuera de este análisis: textos espejo (M1), blips espejo (M2,
`CMirrorBlips`), cilindros rojos nativos (`CMissionAreaMarkers`), cámara espejo y cinemáticas.

Infraestructura que ya existe y sirve para todas las reglas de abajo:
- **Gancho post-comando**: `COpCodeSync` redirige `0x469FF3` (`CRunningScript::Process`, antes del handler) y
  `FinishedOpcodeProcessing_Hook` (después): se ve cada comando con su script (`lastProcessedScript`), sus parámetros
  (`ScriptParams`) y el resultado (flag de comparación). `CommandHooks::GetCurrentScript()` da el script dentro de los
  handlers. `FinishedProcessingScripts` (`0x46A22E`) corre una vez por frame al terminar todos los scripts.
- **Inicio de misión**: `CMissionMirror::OnMissionLaunched` ya engancha `0417 LOAD_AND_LAUNCH_MISSION_INTERNAL` (id en
  `ScriptParams[0]`, script nuevo = cabeza de `pActiveScripts`, `m_bIsMission`), y detecta el fin cuando el script sale de
  la lista activa.
- **Quién participa**: `CCoopMission::IsScriptAuthority()` y `ShouldIncludeNetworkPlayer()` ya resuelven F1
  individual/coop, misiones siempre individuales, llegada tarde (`m_bLateForMission`).

Opcodes citados (de `plugin_sa/game_sa/eScriptCommands.h`): `00A1` SET_CHAR_COORDINATES, `0362`
WARP_CHAR_FROM_CAR_TO_COORD, `036A` WARP_CHAR_INTO_CAR, `0430` WARP_CHAR_INTO_CAR_AS_PASSENGER, `0173` SET_CHAR_HEADING,
`0860` SET_CHAR_AREA_VISIBLE, `04BB` SET_AREA_VISIBLE, `016A` DO_FADE, `01B4` SET_PLAYER_CONTROL, `02E4/02E7/02EA`
LOAD/START/CLEAR_CUTSCENE, `03CB` LOAD_SCENE, `0417` LOAD_AND_LAUNCH_MISSION_INTERNAL, `03A4` SCRIPT_NAME, `0317`
REGISTER_MISSION_GIVEN, `00D8` MISSION_HAS_FINISHED, `018A` ADD_BLIP_FOR_COORD, `0164` REMOVE_BLIP, `06D5`
CREATE_CHECKPOINT, `00FE/00FF/0100` LOCATE_CHAR_ANY_MEANS/ON_FOOT/IN_CAR_3D, `00BC` PRINT_NOW, `03E5` PRINT_HELP, `0595`
REGISTER_ODDJOB_MISSION_PASSED, `030C` PLAYER_MADE_PROGRESS.

## Conteo total (rg sobre `scm/scripts`)

| Comando | Llamadas | Comando | Llamadas |
|---|---|---|---|
| RemoveCheckpointForAllNetworkPlayers | 833 | UpdateCheckpointForNetworkPlayer | 24 |
| TeleportAllNetworkPlayersToHost | 661 | UpdateCarBlipForNetworkPlayer | 24 |
| RemoveCharBlipForAllNetworkPlayers | 609 | ChooseSideMissionMode | 21 |
| UpdateCharBlipForAllNetworkPlayers | 560 | RemoveCheckpointForNetworkPlayer | 18 |
| UpdateCheckpointForAllNetworkPlayers | 545 | Remove/UpdateCharBlipForNetworkPlayer | 10 + 10 |
| RemoveCarBlipForAllNetworkPlayers | 528 | RemoveCarBlipForNetworkPlayer | 12 |
| UpdateCarBlipForAllNetworkPlayers | 336 | PrintForNetworkPlayer / ClearAllEntityBlipsForNetworkPlayer | 9 + 9 |
| IsNetworkPlayerActorValid | 330 | MinigameVsReport | 8 |
| PrintNowForNetworkPlayer | 186 | IsHost | 6 |
| ClearAllEntityBlipsForAllNetworkPlayers | 128 | PrintHelp / IsSyncingThisPed / GetPedInAreaWithModel / ClaimPedOnRelease / CancelPedClaim / AddChatMessage | 4 c/u |
| EnableSyncingThisScript | 100 | LocateAllPlayersOnFoot3D / ClearThisPrintForNetworkPlayer | 3 c/u |
| CollectNetworkPlayersForTheMission | 47 | TeleportPlayersToHostSafely / PutNetworkPlayersInCar / PedResetAllClaims | 2 c/u |
| | | PedTakeHost / LaunchMissionForNetworkPlayers / IsCoopMission / GrantOddjobReward / GetNetworkPlayerInternalId | 1 c/u |

Los blips "para todos" (≈2160) ya quedan mudos con `CMirrorBlips::MutesCoopBlipCommands()`; no se re-analizan.

---

## 1. `Coop.EnableSyncingThisScript` (100 llamadas, 100 scripts)

**Qué hace en C++** (`CCommandEnableSyncingThisScript.cpp`): agrega los 7 primeros caracteres de `m_szName` a
`COpCodeSync::ms_aszSyncedScripts` (lista que solo crece en la sesión; nunca se borra). Después, cada comando de un
script con ese nombre se replica si `IsScriptAuthority() && ms_bSyncingEnabled` (`COpCodeSync::IsOpcodeSyncable`).
**No mira nada más**: ni coop/individual ni host. La condición está en los consumidores: `IsScriptAuthority()`
(el host que juega solo no es autoridad), `ShouldIncludeNetworkPlayer()` y `COpCodeSync::IsScriptSynced()`.

**Patrón: siempre incondicional y siempre en la línea siguiente a `script_name`** (verificado en los 100: ninguno
está dentro de un `if`). Ejemplos:
```
SWEET2.txt:5   script_name 'SWEET2'
SWEET2.txt:6   Coop.EnableSyncingThisScript()
CATALIN.txt:15 script_name 'CATALIN'          (tras $onmission = 1 y Stat.RegisterMissionGiven)
CATALIN.txt:16 Coop.EnableSyncingThisScript()
INT.txt:2      script_name 'INT'               (lanzador de MAIN, no misión: INTRO1/INTRO2)
INT.txt:3      Coop.EnableSyncingThisScript()
```
Son las 99 misiones de historia con `Coop` + `INT`. **Lo condicional son las secundarias (21)**: arrancan con un nombre
"SOLO" (`script_name 'BCOSOLO'`, `'AMBSOLO'`) que no está registrado, y `Coop.ChooseSideMissionMode` (si sale coop)
renombra el script al nombre real y llama a `EnableScriptSyncing` (`CCoopMission::ResolveWaitingChoices`). O sea: en
historia la sincronización es fija; en secundarias depende de la elección Y/N.

Misiones sin `EnableSyncing` (corren locales en cada uno o sin coop): `SWEET6` (Cesar Vialpando, siempre individual),
`LOWR`, `ZERO5`, `LA1FIN` (el de MAIN; `LA1FIN2` sí), `DESERT5` (secundaria con ChooseSide), lanzadores (`SWEET`,
`SMOKE`, `RYDER`...), tiendas, gimnasios, etc.

**Regla genérica propuesta**: registrar en `CMissionMirror::OnMissionLaunched` (gancho de `0417`, ya existe) por **id de
misión**, no por nombre: el id está en `ScriptParams[0]` y no depende de que `03A4 script_name` haya corrido.
- Tabla en C++ `id → {historia-coop | secundaria-elegible | individual-siempre | local}`: historia → `EnableScriptSyncing`
  del script nuevo (puntero, no nombre); secundaria → abre la elección (sección 7); 0–10, `SWEET6`=19, escuelas, taxi,
  pimp → nada.
- `INT` (no es misión): registrar por nombre en un gancho de `03A4` en scripts de MAIN (solo `INT`), o mejor eliminarlo
  (ver 9: su única razón es `LocateAllPlayersOnFoot3D`).
- Riesgos: (a) la lista actual compara **por nombre**: sub-scripts que la misión lanza con `start_new_script` y luego
  `script_name` igual heredan la sync; si se pasa a puntero hay que replicar eso con la regla de "IP dentro del bloque de
  misión" (`0xA7A6A0`..`+69000`, mirror-layer.md §3.2 de `project_gta_sa_coop`, GPL). (b) El registro tiene que estar
  antes del primer comando que haya que replicar (fade/cinemática al principio): `0417` cumple. (c) Mantener el
  interruptor y log.

---

## 2. Teletransportes: `TeleportAllNetworkPlayersToHost` (661 / 108 scripts), `TeleportPlayersToHostSafely` (2), `PutNetworkPlayersInCar` (2)

**Qué hacen en C++**:
- `TeleportAllNetworkPlayersToHost` (sin parámetros): si el script está sincronizado y el local es autoridad, toma hasta
  3 jugadores con `ShouldIncludeNetworkPlayer` y llama a `TeleportToHost`: posiciones libres en un arco detrás del host
  (1–5 m, `FindZCoorForPed`, `TestSphereAgainstWorld`, línea de visión) y manda `TeleportPlayerScripted` (pos + heading del
  host). El receptor hace `Teleport(pos)` + heading. **No manda interior** (`m_nAreaCode`): depende de que el
  `Char.SetAreaVisible($scplayer)`/`04BB` replicado lo arregle.
- `TeleportPlayersToHostSafely(p1,p2,p3)`: igual con handles explícitos (SWEET3:1209; SWEET1B:1589 comentado).
- `PutNetworkPlayersInCar(car)`: por cada jugador, asiento libre del auto (`PutInVehicleScripted`), y si no hay, un auto
  igual (modelo/colores) 7–55 m detrás del host. Sin auto en red: cae a `TeleportToHost`. Usos: `SWEET2.txt:2810`
  (`$big_smoke_car`), `HOODS5.txt:1218`.

**Patrón (medido): 657 de 661 van inmediatamente después de mover al jugador local**:
| Disparador previo (≤4 sentencias antes) | Llamadas |
|---|---|
| `Char.SetCoordinates($scplayer, …)` (00A1) | 495 |
| `Char.WarpFromCarToCoord($scplayer, …)` (0362) | 100 |
| `Char.WarpIntoCar[AsPassenger]($scplayer, car)` (036A/0430) | 62 |
| otro (SWEET1:1892, SWEET1B:2447, SWEET3:367, TWAR7:3126, misiones por jugador) | 4 |

Ejemplos:
```
BCESAR4.txt:370-373  Text.LoadMissionText('BCESAR4') / World.ClearArea(…) /
                     Char.SetCoordinates($scplayer, -495.9034, -196.2218, 77.3986)
                     Coop.TeleportAllNetworkPlayersToHost()
KICKSTA.txt:239-243  Streaming.SetAreaVisible(14) / Streaming.RequestCollision(…)
                     Char.SetCoordinates($scplayer, -1473.187, 1565.173, 1051.589)
                     Coop.TeleportAllNetworkPlayersToHost()
                     Char.SetAreaVisible($scplayer, 14)        <- el interior viene DESPUÉS
CASINO1.txt:632-636  if Char.IsInCar($scplayer, 322@) …
                     Char.WarpFromCarToCoord($scplayer, 1903.27, 955.95, 9.8203)
                     Coop.TeleportAllNetworkPlayersToHost()
```
Contexto típico alrededor (frecuencia en ±6 líneas): antes `World.ClearArea` (115), `Char.ClearTasksImmediately` (103),
`Streaming.LoadScene` (52), `Camera.SetFixedPosition/PointAtPoint` (≈50), `Hud.SwitchWidescreen` (43); después
`Char.SetHeading` (398!), `Camera.SetBehindPlayer` (101), `Camera.RestoreJumpcut` (71), `Camera.DoFade` (69),
`Player.SetControl` (55). Es decir: **reposicionamientos de inicio/fin de cinemática y de cambio de fase**, casi siempre
a pantalla negra o con widescreen.

Cobertura inversa: en los scripts sincronizados hay 811 movimientos del jugador local (605 SetCoordinates, 129
WarpFromCar, 77 WarpIntoCar); 662 (82 %) ya tienen teletransporte. De los 149 sin él, ~70 están en las misiones por
jugador (SWEET2 15, INTRO1 11, TWAR7 11, SWEET1B 8, SWEET1 7, INTRO2 6, SMOKE2 6, SWEET3 4…), que ubican a cada
invitado a mano; el resto son ramas olvidadas (p.ej. `GUNS1.txt:1776-1780`: el `SetCoordinates` tiene teletransporte y
el `WarpFromCarToCoord` de la otra rama no), dos warps seguidos (GROVE2:706-708) o saltos de depuración (CASINO2
`$c2_cp`).

**Regla genérica propuesta (alta cobertura, bajo riesgo)**: en el gancho post-comando, si el comando es `00A1`, `0362`,
`036A` o `0430`, el char es `FindPlayerPed(0)`, el script es de la misión del dueño (sincronizado, `m_bIsMission` o IP en
el bloque de misión) y el local es autoridad → **marcar "teleport pendiente"**. Ejecutarlo **cuando el script cede**
(`wait`, o en `FinishedProcessingScripts`), no en el acto: así se toma el heading final (`0173` va después en 398 casos)
y el interior final (`0860`/`04BB` después, como KICKSTA). Varios warps en el mismo frame → un solo teletransporte.
- Para `036A/0430` (host metido en un auto): usar la lógica de `PutNetworkPlayersInCar(car)` (asientos o auto clon) en
  vez del arco a pie. Es lo que hacen a mano SWEET2/HOODS5. Riesgo: autos clon que la misión no conoce (no los borra el
  cleanup: hay que borrarlos al terminar la misión).
- Agregar `m_nAreaCode` del host al paquete `TeleportPlayerScripted` (hoy no viaja).
- Riesgos: (a) movimientos chicos que no son "cambio de lugar" (acomodar 1 m antes de una cinemática): también hoy se
  teletransporta, así que no cambia; opcional umbral (no mover si el invitado está a < 30 m y mismo interior).
  (b) Misiones que mueven al host en bucle cada frame (no se vio ninguna sincronizada; escuelas/minijuegos son
  individuales). Poner tope (1 por 500 ms). (c) Movimiento en el fallo/limpieza de la misión: inocuo, la misión falla
  para todos. (d) Las 11 misiones por jugador ubican a cada invitado en lugares distintos: la regla no debe pisar eso
  mientras sigan con su lógica propia (lista de exclusión o regla por misión).

**No encaja genérico**: SWEET1:1892 (teletransporte al empezar una mini-escena sin mover al host), SWEET3:367/1209,
SWEET1B:2447, TWAR7:3126 (cada invitado a un asiento/lugar distinto), SWEET2 (los 15 warps al auto de Smoke), INTRO1
(cada uno a su BMX), DRUGS1 (cada uno a su Uzi-auto). Ver 4.

---

## 3. Otros jugadores: `IsNetworkPlayerActorValid` (330 / 14), `CollectNetworkPlayersForTheMission` (47 / 47), `GetNetworkPlayerInternalId` (1)

**Qué hacen**: `Collect…` devuelve hasta 3 handles de ped de los jugadores incluidos (0 en huecos) → `$NETWORK_PLAYER[0..2]`.
`IsNetworkPlayerActorValid(h)` = el handle es un jugador de red **y** `ShouldIncludeNetworkPlayer` (filtra individuales y
tardíos en cada llamada). `GetNetworkPlayerInternalId` = índice de `CWorld::Players` del remoto (para leer su ropa).

**Patrón de `Collect`**: una vez, al empezar la misión, pegado a `Stat.RegisterMissionGiven` / `$onmission = 1`
(`SWEET1.txt:60-62`, `HOODS5.txt:32-34`, `INTRO1.txt:117`) o tras `ChooseSideMissionMode` en secundarias
(`BCOUR.txt:56`). **En 33 de los 47 scripts el resultado no se usa nunca** (BCOUR, BLOOD, BOAT, CESAR1, GUNS1, MUSIC1-5,
RYDER2/3, SMOKE3, SWEET7, LA1FIN2, CRASH1/4, DRUGS3, secundarias…): código muerto heredado.

**Patrón de `IsNetworkPlayerActorValid`**: solo en 14 scripts, casi todo en las 11 por jugador (SWEET3 90, TWAR7 40,
SWEET1 39, SMOKE2 36, INTRO1 33, INTRO2 29, SWEET1B 22, SWEET4 10, COPCAR 6, DRUGS1 6, SWEET2 6) + HOODS5 11, AMBULAN 1,
POOL2 1. Siempre `for $temp_int = 0 to 2 / if Coop.IsNetworkPlayerActorValid($NETWORK_PLAYER[$temp_int]) then … end`.
Lo que se hace con cada otro jugador (comandos con `$NETWORK_PLAYER[i]`):
| Uso | Llamadas | Dónde |
|---|---|---|
| Texto por jugador (`PrintNow/Print/PrintHelpForNetworkPlayer`) | ≈200 | las 11 + HOODS5 |
| Ubicarlo (`SetCoordinates`+`SetHeading`+`ClearTasksImmediately`, `SetAreaVisible`) | 45+35+18+8 | INTRO1/2, SMOKE2, TWAR7, SWEET1/1B |
| **Esperarlo** en un punto (`LocateAnyMeans3D`, `LocateOnFoot3D`, `LocateAnyMeansChar*`) | ≈36 | INTRO1/2, SWEET1/1B/3/4, TWAR7, SMOKE2 |
| Saber si está en el auto (`IsInAnyCar`, `IsInCar`, `IsOnAnyBike`) | 31+10+3 | las 11 |
| Blips/checkpoints por jugador | ≈80 | las 11 |
| Meterlo en un auto (`WarpIntoCar[AsPassenger]`, `Task.EnterCarAs*`) | 12+5 | DRUGS1, SWEET3, TWAR7 |
| Darle armas (`GiveWeapon`, `GetWeaponInSlot`, `HasGotWeapon`) | 7+2+1 | DRUGS1 (Uzi), SWEET1, SWEET3, TWAR7 |
| Paramédico: el que lleva al paciente | 3 | AMBULAN (`AMBULAN_FINDMEDIC`) |
| Leer su ropa | 1 | INTRO2:119 (ya no se usa en la versión actual) |
No hay `SetHealth`, `Group.SetMember` ni armadura sobre otros jugadores.

Ejemplo de "esperar a todos" (`TWAR7.txt:529-542`):
```
if Char.LocateInCar3D($scplayer, 1532.8, -1672.9, 12.4, 4.0, 4.0, 4.0, True)
207@ = 1 // all the network players are at the Police Station
for $temp_int = 0 to 2
    if Coop.IsNetworkPlayerActorValid($NETWORK_PLAYER[$temp_int])
        if not Char.LocateAnyMeans3D($NETWORK_PLAYER[$temp_int], 0, 1532.8, -1672.9, 12.4, 12.0, 12.0, 4.0)
            207@ = 0
```
Mismo patrón en `SWEET4.txt:1471-1485` (nuestro, cutscene de los Ballas) e `INTRO2` (peluquería).

**Reglas genéricas propuestas**:
- `Collect` → no hace falta: `CCoopMission` ya tiene los participantes; el script vanilla no usa `$NETWORK_PLAYER`.
- **"Esperar a todos en el cilindro"**: en el gancho post-comando de los `LOCATE_*` con esfera = 1 (`00FE/00FF/0100`, los
  `LOCATE_STOPPED_*` y los 2D) sobre `FindPlayerPed(0)` en la misión del dueño: si el resultado es verdadero, AND con
  "cada participante está a ≤ max(3·radio, 12 m) del centro (a pie o en auto)"; si alguno falta, devolver falso y avisar
  por chat/texto "esperando a X" (cada 5 s). Es exactamente lo que hacen a mano TWAR7/SWEET4/INTRO2 y la regla fija del
  proyecto ("las misiones esperan a todos"). `CMissionAreaMarkers` ya engancha los 14 sitios de
  `HighlightImportantArea`, así que se sabe qué LOCATE dibuja cilindro. Riesgos: (a) LOCATE con esfera que no son
  "llegada" (raros; p.ej. un checkpoint de carrera o una persecución): lista de exclusión por misión/punto; (b) un
  invitado que no puede llegar (bug, cayó al agua) traba la misión → comando de chat `/traer` ya existe; (c) un LOCATE
  que el script evalúa con `not` como condición de fallo ("te alejaste"): con esfera = 1 casi nunca, pero hay que
  loguear y revisar.
- Armas/autos extra por jugador → por misión (sección 11).

---

## 4. Checkpoints: `Update/RemoveCheckpointForAllNetworkPlayers` (545 / 97 y 833 / 119), `…ForNetworkPlayer` (24 / 8 y 18 / 6)

**Qué hacen**: mandan `UpdateCheckpoint {pos, radio}` / `RemoveCheckpoint` a cada participante (solo autoridad + script
sincronizado; no los muta `CMirrorBlips`). El receptor dibuja **un** checkpoint propio (`CNetworkCheckpoint`, uno solo
a la vez). `…ForNetworkPlayer` = lo mismo para un jugador.

**Patrón**:
- `Update` va **pegado a `Blip.AddForCoord`** (534 de 545, ≤3 líneas antes) con las mismas coordenadas y radio 4.0
  (`RYDER2.txt:601-602`, `RYDER2.txt:1660-1661`). Los otros 11: CPRACE/MTBIKER/TRIA (tras cambiar color/escala del blip
  de carrera), SWEET1B/SWEET2/INTRO2 (por jugador).
- `Remove` va **pegado a `Blip.Remove`** (692 de 833 después) y en la limpieza: antes de
  `ClearAllEntityBlipsForAllNetworkPlayers` (122) y `Mission.Finish` (77).
- Carreras/secundarias (`BCOUR.txt:812-816`): además del `Update`, el script crea `Checkpoint.Create` (`06D5`, el
  checkpoint de carrera del juego).
```
RYDER2.txt:600  Blip.Remove(120@)
RYDER2.txt:601  120@ = Blip.AddForCoord(2699.293, -2395.013, 12.6172)
RYDER2.txt:602  Coop.UpdateCheckpointForAllNetworkPlayers(2699.293, -2395.013, 12.6172, 4.0, 4.0, 4.0)
```
**Regla genérica**: ya cubierta en su mayor parte. El checkpoint del receptor es un sustituto del cilindro que el juego
dibuja con el `LOCATE` de esfera, y eso ya lo hace `CMissionAreaMarkers` de forma nativa. Lo que falta:
- Si se quiere conservar algo atado al blip: en `CMirrorBlips`, al espejar un blip de coordenada (`018A`) de la misión,
  no hace falta checkpoint; el cilindro lo da el LOCATE. → **Borrar `CNetworkCheckpoint`** cuando `CMissionAreaMarkers`
  esté probado en juego.
- **Checkpoints de carrera `06D5`** (`ScriptCheckpointArray` 0xA44070, `AddScriptCheckpoint` 0x4935A0,
  `RemoveScriptCheckpoint` 0x4936C0; mirror-layer.md §3.3): espejarlos con un gancho post-comando de `06D5`/`06D6` (CREATE/DELETE_CHECKPOINT) en la
  misión del dueño. Afecta a BCOUR, CPRACE, MTBIKER, TRIA, STUNT, BOAT y misiones de historia con carreras.
- Limpieza: al terminar la misión (`00D8`/fin detectado por `CMissionMirror`) borrar todo (ya lo hace
  `CCoopMission.cpp:719`).

**No genérico**: los `…ForNetworkPlayer` de las misiones por jugador (sección 5).

---

## 5. Blips por jugador: `Update/Remove{Car,Char}BlipForNetworkPlayer`, `ClearAllEntityBlipsForNetworkPlayer` (≈65 llamadas, 11 misiones)

**Qué hacen**: blip sobre un auto/ped para **un** jugador (difiere hasta 3 s si la entidad aún no tiene id de red).
`CMirrorBlips` no espeja estas 11 misiones (COPCAR DRUGS1 INTRO1 INTRO2 SMOKE2 SWEET1 SWEET1B SWEET2 SWEET3 SWEET4 TWAR7).

**Lógica por jugador exacta** (misión por misión):
| Misión | Qué es distinto por jugador |
|---|---|
| INTRO1 | Cada invitado tiene **su BMX** (`$NETWORK_PLAYER_VEHICLE[i]`); blip en su bici mientras no está montado, "súbete a la bici" (`INTRO2E/G/J`), checkpoint al destino cuando está montado; ubicarlos al lado de la casa tras `INTRO1A`. |
| INTRO2 | Peluquería: espera a que todos salgan a pie; blip/auto propio; destino distinto si no está en auto. |
| SWEET1 | Jugadores 1-2 en el auto de Sweet, el 3º en **el auto de Ryder** (`SWEET1.txt:487-500`); por cada uno: si no está en "su" auto → blip en el auto + "sube al auto"; si está → checkpoint al destino (`SWEET1.txt:635-654`). Armas al final. |
| SWEET1B | Limpieza del barrio: cada uno con sus dealers/blips; checkpoints propios; teletransporte comentado. |
| SWEET2 | Todos al auto de Smoke (`PutNetworkPlayersInCar`), blip en el auto mientras no estén. |
| SWEET3 | Cada uno a un Glendale: blip azul en el auto, Uzi si no tiene subfusil (`SWEET3.txt:369-380`), textos "sube al auto" solo a quien está a pie (`SWEET3.txt:1985-1997`). |
| SWEET4 | Blips de los Ballas por jugador; espera a todos en el marcador (nuestro). |
| SMOKE2 | Blips en los dos autos (`SMOKE2.txt:238-250`), blip rojo del pandillero, "persigue". |
| TWAR7 | Todos al Greenwood (el primero maneja, el resto pasajeros, `TWAR7.txt:184-200`), espera a todos en la comisaría y en lo de Freddy. |
| DRUGS1 | **Un auto extra por invitado** (`DRUGS1.txt:2556-2584`: `Car.Create(581…)` + blip + `GiveWeapon(MicroUzi, 500)`). |
| COPCAR | Vigilante: textos de tiempo por jugador, recompensa (sección 7). |

Patrón común: es la máquina de estados vanilla "sube al vehículo X / llega a Y" **replicada por jugador**, más entidades
extra (autos/bicis) que el original no tiene porque hay más gente que asientos.

**Regla semi-genérica posible ("objetivo de vehículo")**: cuando la misión del dueño tiene un blip de auto sobre V y el
host está en V, cada invitado que no está en V (ni en un auto clon de V) ve el blip sobre V y el texto del host
correspondiente; al estar en V/clon ve los blips de coordenada. Más `PutNetworkPlayersInCar` disparado por el warp del
host (sección 2) para crear el clon cuando no hay asientos. Riesgo medio: depende de identificar "el auto de la
misión" (el que tiene blip `0186` y en el que el script pide al jugador subir: `IS_CHAR_IN_CAR($scplayer, V)` en bucle).
Cubriría parte de SWEET1, SWEET2, SWEET3, SMOKE2, TWAR7. El resto (BMX propias, autos extra de DRUGS1, el 3º en el auto
de Ryder) es por misión.

---

## 6. Textos por jugador: `PrintNow/Print/PrintHelp/ClearThisPrintForNetworkPlayer` (186 + 9 + 4 + 3)

**Qué hacen**: mandan `AddMessageGXT`/`RemoveMessageGXT` a un jugador (sin chequear autoridad).

**Patrón**: 155 de 188 son `Text.PrintNow('X')` seguido del bucle por jugador con **la misma clave** (redundante con el
espejo de textos M1 que ya manda los textos del dueño):
```
SWEET1.txt:486  Text.PrintNow('SWE1_A', 7000, 1)
SWEET1.txt:487  for $temp_int = 0 to 1 …
SWEET1.txt:491      Coop.PrintNowForNetworkPlayer('SWE1_A', 7000, 1, $NETWORK_PLAYER[$temp_int]) // Get in Sweets Car
```
Los otros 33 son **textos que solo ve el invitado** según su propio estado (no está en su auto, se bajó de la bici,
"vuelve al auto"): INTRO1 (7, `INTRO2E/G/J/K`), SWEET1 (8, `HOOD3_A/B`, `SWE1_S/B`, `JUMPH1`), SWEET1B (6), SWEET3 (4),
SMOKE2 (2), SWEET2 (2), TWAR7 (2), INTRO2 (2). HOODS5 usa `PrintForNetworkPlayer` (8) igual al texto del host
(redundante). COPCAR: `C_TIME` (redundante).

**Regla genérica**: ninguna nueva; el espejo M1 ya cubre los 155 redundantes. Los 33 dependen del estado del invitado →
van con la regla "objetivo de vehículo" (sección 5) o por misión.

---

## 7. Secundarias y modos: `ChooseSideMissionMode` (21), `GrantOddjobRewardToNetworkPlayers` (1), `MinigameVsReport` (8), `LaunchMissionForNetworkPlayers` (1), `IsCoopMission` (1), `IsHost` (6)

**`ChooseSideMissionMode('NAME', "Título")`** (C++ `CCoopMission::ChooseSideMissionMode`): devuelve falso mientras se decide
(oferta de 12 s a los demás); al decidir coop, renombra el script a `NAME` y lo sincroniza. CPRACE va al lobby de
carreras (`CVsLobby`). Individual-siempre (`IsIndividualOnlyScript`) o jugador solo → individual directo.
Patrón idéntico en los 21 (AMBULAN BCOUR BLOOD BOAT BSKOOL BURGJB COPCAR CPRACE DESERT5 DSKOOL FIRETRU FREIGHT KICKSTA
MTBIKER PIMP QUARRY SHRANGE STUNT TAXIODD TRIA TRUCK), al principio, tras `$onmission = 1`:
```
BCOUR.txt:5    script_name 'BCOSOLO'
BCOUR.txt:51   :BCOUR_COOP_CHOOSE
BCOUR.txt:52   wait 0
BCOUR.txt:54   if Coop.ChooseSideMissionMode('BCOUR', "Mensajero")
BCOUR.txt:55   goto_if_false @BCOUR_COOP_CHOOSE
BCOUR.txt:56   $NETWORK_PLAYER[0..2] = Coop.CollectNetworkPlayersForTheMission()
```
**Regla genérica**: en el gancho de `0417` con id de secundaria (113–134 y las que estén en la tabla): crear la elección y
**retener el script** sin tocarlo: mientras la elección esté en curso, en el gancho de `CRunningScript::Process` (o
poniendo `m_nWakeTime = ahora + 1` cada frame) el script no avanza; al decidir, se suelta. Renombrar ya no hace falta si la
sync se registra por puntero (sección 1). Riesgos: el script vanilla hace `wait 0` + `fade` al principio; retenerlo antes
del primer comando es lo más limpio (justo tras `0417`). Las de "siempre individual" (escuelas, taxi, pimp) ni se
retienen.

**`GrantOddjobRewardToNetworkPlayers(id, $flag, p1, p2, p3)`**: manda `OddjobReward {id, offset del flag}` a cada
participante (el receptor da la recompensa y marca su flag). Único uso `COPCAR.txt:2522` (nivel 12 de Vigilante, solo si
`IsCoopMission`). Regla genérica: gancho de `0595 REGISTER_ODDJOB_MISSION_PASSED` / escritura del flag `$done_*_progress`
en la misión coop del dueño → recompensa a cada participante. Pero cada secundaria tiene su recompensa (Vigilante: +50 %
armadura; Paramédico: salud 150; Bombero: inmune al fuego; Taxi: nitro…) → tabla por id (por misión, chica).

**`MinigameVsReport(game, score, state)`**: reporta puntaje al lobby VS (`CMinigameVs`). DANCE (5) y LOWR (3). Son
minijuegos locales; el gancho genérico posible es leer la variable de puntaje (`$iDanceScore`, `$lowrider_pscore`) por
offset desde C++ cada frame mientras el script corre → por minijuego.

**`LaunchMissionForNetworkPlayers(19)`**: `SWEET6.txt:6`; en el host manda `LaunchMissionLocal` para que cada uno corra
Cesar Vialpando en su juego... pero `IsIndividualOnlyMissionId(19)` lo corta: hoy **no hace nada** (código muerto).

**`IsCoopMission`**: `COPCAR.txt:26` (para la recompensa). Desaparece con la regla de recompensas.

**`IsHost`**: `INTRO.txt:61,1263,1477` (solo el host pone el blip de contacto de Big Smoke; los demás no),
`INTROST.txt:10` (en C++ devuelve verdadero siempre para `INTROST`: es de cada jugador), `POOL2.txt:21`, `JFUD.txt:196`.
Regla genérica: el blip de contacto de `INTRO` es MAIN; con MAIN vanilla cada uno lo tendría → aceptar (progreso por
jugador) o filtrar en invitados los `02A7/02A8 ADD_SPRITE_BLIP_FOR_CONTACT_POINT/COORD` del script `INTRO`. POOL2 y JFUD: ver 8 y 9.

---

## 8. Propiedad de peds: `ClaimPedOnRelease`, `CancelPedClaim`, `PedResetAllClaims`, `PedTakeHost`, `IsSyncingThisPed`, `GetPedInAreaWithModel` (19 en total)

**Qué hacen**: `GetPedInAreaWithModel(pos, radio, m1,m2,m3)` busca un ped no jugador en una caja (con 3 modelos o
cualquiera); `ClaimPedOnRelease` pide al server quedarse ese ped de red cuando el dueño lo suelte;
`CancelPedClaim`/`PedResetAllClaims` lo deshacen; `PedTakeHost` (solo host) toma la propiedad; `IsSyncingThisPed` = el
ped tiene copia de red.

**Patrón** (solo tiendas, scripts externos de MAIN: `BARB`, `JFUD`, `PSHOP`): en vez de crear el vendedor, reusar el que ya
existe en red (si otro jugador ya está en la tienda), y al salir borrarlo solo si es nuestro:
```
BARB.txt:238  $shop_keep_barbers = Coop.GetPedInAreaWithModel($barberX, …, 4.0, 4.0, 40.0, 156, 177, 176)
BARB.txt:240  if $shop_keep_barbers == -1 then $shop_keep_barbers = Char.Create(4, …) else Coop.ClaimPedOnRelease(…)
BARB.txt:1182 if Coop.IsSyncingThisPed($shop_keep_barbers) then Char.Delete(…) else Coop.CancelPedClaim(…)
```
**Regla genérica**: gancho de `CREATE_CHAR` (`009A`) / `CREATE_CHAR_AT_ATTRACTOR` en esos tres scripts (no misión, por
nombre): si ya hay un ped de red con ese modelo en la caja, devolver su handle en vez de crear y pedir el claim; en
`DELETE_CHAR` (`009B`) del vendedor ajeno, cancelar el claim en vez de borrar. Riesgo: tocar el resultado de un comando
de creación (handle) es delicado; alternativa: **no sincronizar los vendedores** (cada uno ve el suyo, son peds locales
de tienda), que es lo que haría el juego vanilla. Recomendación: lo segundo (más simple, sin riesgo de handles), salvo que
el usuario quiera ver el mismo vendedor.

---

## 9. Resto: `LocateAllPlayersOnFoot3D` (3), `AddChatMessage` (4)

**`LocateAllPlayersOnFoot3D(sphere, pos, radio)`** (C++ 160 líneas): marcador de inicio que espera a todos (no a los
individuales), con el contador de Sweet leído de `ScriptSpace[1808]`, aviso de progreso distinto, bloqueo
`CMissionLock`, y "Cesar Vialpando es individual". Usos: `SWEET.txt:19`, `INT.txt:20` y `:38` (marcadores de
Big Smoke y Ryder). **Los demás lanzadores (SMOKE, RYDER, …) usan el vanilla `Char.LocateOnFoot3D($scplayer …)`**
(`SMOKE.txt:19`), o sea que hoy no esperan a todos en el marcador de inicio salvo SWEET e INT.

Regla genérica: gancho post-comando de `00FF LOCATE_CHAR_ON_FOOT_3D` sobre `FindPlayerPed(0)` en scripts de MAIN que no
son misión **y que en las sentencias siguientes llaman a `0417`** (los lanzadores; se puede detectar por tabla de nombres:
`SWEET SMOKE RYDER INT CESAR …`, o por "el script lanzó una misión antes"): aplicar la misma lógica que
`CCommandLocateAllPlayersOnFoot3D` (esperar a los incluidos, chequear progreso, `CMissionLock`). Así cubre **todos** los
lanzadores con MAIN vanilla. Riesgo: lanzadores que usan el mismo LOCATE para otras cosas (radio de entrada a un
interior); la tabla de nombres lo acota. El contador de Sweet por offset sigue valiendo con MAIN vanilla (es el
`$flag_sweet_mission_counter` del original; verificar el offset en el `main.scm` vanilla).

**`AddChatMessage("…")`**: solo texto de chat local: POOL2 (Y/N), INTRO2:663 ("esperando a que todos salgan de la
peluquería"). Lo absorbe la regla "esperar a todos" (sección 3).

**POOL2** (billar): `IsHost` + `Collect` + elección Y/N propia por `Pad.IsButtonPressed` → minijuego VS: por minijuego.

---

## 10. Nuestros cambios desde el tag `base`

`git diff --stat base..HEAD -- scm/scripts`: 24 archivos, +204 −1040.
- **Las 21 secundarias** (AMBULAN BCOUR BLOOD BOAT BSKOOL BURGJB COPCAR CPRACE DESERT5 DSKOOL FIRETRU FREIGHT KICKSTA
  MTBIKER PIMP QUARRY SHRANGE STUNT TAXIODD TRIA TRUCK; −49/+6 cada una): se sacó el elector Y/N escrito en SCM (que en
  `base` corría `if Coop.IsHost()`, `Collect`, bucle de `IsNetworkPlayerActorValid`, `Pad.IsButtonPressed(DpadRight/Left)`
  5 s, y si era coop `script_name 'BCOUR'` + `Coop.EnableSyncingThisScript()` + chat) y se reemplazó por el bucle de
  `Coop.ChooseSideMissionMode` (decisión en C++, oferta a los demás). Es decir, **en `base` la sync de secundarias ya era
  condicional** y ahora lo es desde C++.
- **AMBULAN** (además): `AMBULAN_FINDMEDIC` (nuevo, +36): el paciente sube a la ambulancia de **cualquier** jugador de la
  misión; `290@` = el jugador médico, y los `LocateInCarChar2D/LocateStoppedInCar3D/StoreCarIsInNoSave($scplayer…)`
  pasan a `290@`. Lógica por misión (no genérica): en C++ sería "en PARAMEDIC, los chequeos sobre `$scplayer` aceptan
  también a un participante en una ambulancia".
- **HOODS5** (+2 −1): tiempo `35@ += 2000` → `500` (un temporizador más corto) y un
  `Coop.RemoveCheckpointForAllNetworkPlayers()` que faltaba tras `$car = Char.StoreCarIsInNoSave($scplayer)`.
- **INTRO2** (+18 −8): la cinemática de afuera de la peluquería espera a que **todos** estén afuera a pie (antes: que
  cada uno hubiera cambiado el pelo, comparando ropa con `GetNetworkPlayerInternalId`), con chat "Esperando a que todos
  salgan"; y el punto de espera de la casa de Sweet se movió al cilindro rojo real (2473.88, −1692.81) a pie o en auto.
- **SWEET4** (+15): la cinemática de los Ballas espera a que todos estén a ≤12 m del marcador (`54@,55@,56@`).

INTRO2 y SWEET4 son exactamente la regla genérica "esperar a todos en el cilindro" (sección 3); AMBULAN es por misión.

---

## 11. Qué no se puede hacer genérico (lista por script)

| Script | Lógica propia que necesitaría regla C++ por misión |
|---|---|
| INTRO1 | BMX por invitado (`$NETWORK_PLAYER_VEHICLE`), "súbete a la bici" por jugador, ubicación de cada uno tras la cinemática de la casa. |
| INTRO2 | Auto/destino por jugador (lo de esperar en la peluquería sí es genérico). |
| SWEET1 | 3º jugador en el auto de Ryder; estado "sube al auto / ve al destino" por jugador; armas al final. |
| SWEET1B | Dealers/blips por jugador, checkpoints propios. |
| SWEET2 | Todos al auto de Smoke (lo cubre el warp→`PutNetworkPlayersInCar` si se acepta el auto clon). |
| SWEET3 | Un Glendale por jugador + Uzi si no tiene subfusil; textos solo a quien está a pie. |
| SWEET4 | Blips de los Ballas por jugador (la espera es genérica). |
| SMOKE2 | Blips de los dos autos y del pandillero por jugador. |
| TWAR7 | Asignación de asientos del Greenwood (el primero maneja); ubicaciones distintas. |
| DRUGS1 | Auto extra (581) + Micro Uzi por invitado. |
| COPCAR | Recompensa de Vigilante a cada participante (tabla de recompensas). |
| AMBULAN | El paciente sube a la ambulancia de cualquier participante. |
| HOODS5 | `PutNetworkPlayersInCar(59@)` (genérico con el warp) y el temporizador 500 ms (ajuste propio). |
| Secundarias con recompensa (PARAMEDIC, FIRETRU, TAXIODD, BCOUR…) | Recompensa por id a cada participante. |
| DANCE, LOWR, POOL2 | Puntaje/lobby VS: leer variables por offset por minijuego. |
| BARB, JFUD, PSHOP | Vendedor compartido (o dejarlos locales, recomendado). |
| INTRO (MAIN) | Blip de contacto de Big Smoke solo en el host. |
| SWEET6 | `LaunchMissionForNetworkPlayers` (hoy no hace nada: se borra). |

Todo lo demás (≈88 misiones de historia: BCESAR4, CASINO*, CAT*, DRIV*, HEIST*, MANSIO*, SYN*, STEAL*, ZERO*, TRUTH*,
WUZI*, RIOT*, FINALE*, …) solo usa: registro de sync, teletransporte tras warp, checkpoints/blips para todos y textos
para todos. **Todo eso es genérico** con las reglas de abajo.

En números: de ≈6000 llamadas, ≈5400 (checkpoints/blips/textos para todos, teletransportes tras warp, registro,
`Collect` muertos) se reemplazan con reglas genéricas o ya están reemplazadas; ≈600 (las 11 misiones por jugador +
AMBULAN + secundarias con recompensa + minijuegos + tiendas) necesitan regla por misión.

---

## 12. Propuesta de orden de implementación

1. **Registro de sync por id en `0417`** (`CMissionMirror::OnMissionLaunched` + tabla id→modo). Riesgo bajo, cobertura
   100 scripts; requisito de todo lo demás. Mientras convivan scripts con `EnableSyncingThisScript`, el comando ya es
   idempotente (no duplica).
2. **Teletransporte tras warp del jugador local** (`00A1/0362/036A/0430`, diferido al `wait`, con interior en el paquete,
   tope 1/500 ms, exclusión de las 11 por jugador). Riesgo bajo, cobertura 657/661 + las 70 ramas olvidadas. Probar con
   los `Coop.TeleportAll…` todavía en los scripts: al ser diferido y único por frame, el doble disparo es inocuo; después
   se compila sin ellos.
3. **Checkpoints**: dar por buenos `CMissionAreaMarkers` en juego y borrar `CNetworkCheckpoint` + espejar `06D5`
   (carreras). Riesgo bajo.
4. **Esperar a todos en el cilindro** (LOCATE con esfera en la misión del dueño) y **marcadores de inicio de todos los
   lanzadores** (generalizar `LocateAllPlayersOnFoot3D` a `00FF` en lanzadores). Riesgo medio (puede trabar misiones si
   un LOCATE no es "llegada"): con lista de exclusión, log de cada espera y `/traer`. Reemplaza INTRO2/SWEET4/TWAR7/INT/SWEET.
5. **Secundarias**: elección retenida en `0417` (sin `ChooseSideMissionMode`) + recompensas por `0595` con tabla. Riesgo
   medio (retener un script).
6. **`PutNetworkPlayersInCar` disparado por `036A/0430`** (auto clon cuando faltan asientos) y la regla "objetivo de
   vehículo" (sección 5). Riesgo medio-alto; cubre parte de SWEET2/SWEET3/SMOKE2/TWAR7/HOODS5.
7. **Reglas por misión** para las 11 + AMBULAN, de a una, probando cada una (empezar por las del principio: INTRO1,
   INTRO2, SWEET1, SWEET1B, SWEET2…), y minijuegos/tiendas al final (o tiendas locales).
8. Recién entonces, fase 6 de `PLAN_CPP.md`: compilar los scripts y `main.scm` originales.

Fuentes: código de `client/src/Commands/Commands/*.cpp`, `COpCodeSync.cpp`, `CCoopMission.cpp`, `CMissionMirror.cpp`,
`CMissionAreaMarkers.cpp`; diseño de referencia `_refs/project_gta_sa_coop/docs/design/mirror-layer.md` §3.2–3.3 y
`docs/mission-porting.md` (GPL-3.0, sametozbalkan).
