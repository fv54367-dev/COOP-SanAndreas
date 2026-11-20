# Referencias open source

## Clonadas el 2026-10-19 en `_refs\` (solo lectura; actualizar con `git pull`)
| Carpeta | Origen | Licencia | Qué sacar de ahí |
|---|---|---|---|
| `project_gta_sa_coop` | github.com/sametozbalkan/project_gta_sa_coop | **GPL-3.0 (reutilizable con atribución)** | Fork del mismo CoopAndreas para el modo historia de 2 jugadores, "reconstruyendo cada misión para coop" en orden de historia. Diseños en `docs/design/`: **`mirror-layer.md`** (capa que espeja texto, blips, locate spheres y timers de la misión del host a los demás sin tocar scripts; no espeja cinemática/cámara/fade, que quedan en `COpCodeSync` y se resuelven por misión en cada port), `world-state.md`, `mission-objects.md`, `trains.md`; `docs/mission-porting.md`, `docs/missions.md` (lista de 103 misiones con riesgos), `docs/test-plan.md`; `CoopAndreas/tests`; su `CLAUDE.md` describe un flujo con Ghidra+ReVa. Incluye CoopTunnel (UDP sobre Steam P2P) e instalador |
| `gta-reversed` | github.com/gta-reversed/gta-reversed | GPL-3.0 | Reimplementación en C++ del exe 1.0 US; buscar por dirección: `grep -rn 0x4D1680 source` |
| `plugin-sdk-sa` | github.com/Dryxio/plugin-sdk-sa | ver LICENSE (plugin-sdk) | SDK con layouts/offsets verificados |
| `reagent` | github.com/Dryxio/reagent | MIT | Agente de ingeniería inversa para reconstruir y validar funciones desde el binario |
| `sa-mcp` | github.com/dntAtMe/sa-mcp | MIT | MCP (Rust) para controlar varias instancias de GTA SA: `compare_instances`, `sync_trace`, `server_netsim`, `launch_instances`, `input`, `run_script`, `read_memory`/`write_memory`, `screenshot`, `server_*` |
| `satk-gta` | github.com/qxlies/satk-gta | MIT | Toolkit + MCP (Python 3.12+): índice de modelos/texturas/mapas, análisis de crash por dirección, opcodes, base de conocimiento del motor |
| `gta-sa-mcp` | github.com/Rams25/gta-sa-mcp | sin LICENSE (no copiar) | MCP + ASI para observar el juego (screenshots, entidades, raycast) |
| `sacoop` | github.com/Parricidium/SACoop | **sin licencia: NO copiar código** | Coop propio (dinput8, UDP 7800, 4 jugadores, host autoritativo) con launcher/lobby; replica a invitados cinemáticas, textos, blips, cámaras, fades y mejoras (60 FPS, widescreen). Solo ideas de producto/diseño |
| `koltrbtw` | fork de CoopAndreas | ver repo | Sectores y traspaso (no copiar su sync de estrellas) |
| `mtasa`, `gtarev` | MTA:SA, reversing varios | ver cada uno | Consulta |

Qué NO existe: el código fuente oficial de GTA SA no se distribuye; lo que hay (gta-reversed, plugin-sdk, símbolos de las versiones móviles que aparecen en los nombres de los crash logs) es reconstrucción. Si el usuario consigue código de la versión móvil, pasarlo a `_refs\` y usarlo solo para entender funciones (no es copiable al proyecto).


# Referencias open-source

> Investigado el 2026-10-19. Copias de solo lectura en `E:\juegos\gtasacoopnuevo\_refs\`: `mtasa\` (sparse), `gtarev\` (sparse), `koltrbtw\` (fork completo). Direcciones de **SA 1.0 US**.
> **Lo más útil:** el fork **koltrbtw/CoopAndreas** (GPL-3) ya implementa el plan (dueño por sectores, handoff en vez de borrar, spawn gateado, relevancia por distancia). Es el mismo código base, así que se puede portar casi directo.

## 1. MTA:SA — multitheftauto/mtasa-blue
### Elección del syncer (`Server/.../CPedSync.cpp`, `CUnoccupiedVehicleSync.cpp`)
| Setting | Default | Qué es |
|---|---|---|
| `ped_syncer_distance` | 100 | distancia máx. a la que el syncer conserva el ped |
| `unoccupied_vehicle_syncer_distance` | 130 | idem autos vacíos |
| `ped_sync_interval` / vehículo | 400 ms | cada cuánto el syncer manda estado |
| `ped_far_sync_interval` | 2000 ms | relay "lejano" a todos |
| `DISTANCE_FOR_NEAR_VIEWER` | 310 | radio de jugadores que reciben el relay a 400 ms |

- **Histéresis:** conserva al syncer si está a <100; para cambiarlo exige <80. Revisa cada 500 ms. Entre candidatos elige **el que menos entidades sincroniza** (balanceo), no el más cercano.
- Autos vacíos: si hay jugador conduciendo, el syncer es siempre el conductor; si hay pasajero jugador, primero él. `OverrideSyncer(entity, player, bPersist)` permite forzar uno persistente (útil para entidades de misión).
- Paquetes: `PED_STARTSYNC` manda al nuevo syncer el último estado conocido (snapshot del server); `PED_STOPSYNC` solo el id; `PED_SYNC` (unreliable sequenced) lleva varias entidades con `ucSyncTimeContext` + flags de campos cambiados. El server descarta si el emisor no es el syncer o el contexto es viejo. Relay a todos cada 2 s o solo a la near list.
- **Copiar:** `ucSyncTimeContext` por entidad (el server lo incrementa al teletransportar/cambiar por script) evita el rebote de posiciones viejas tras un handoff. En el cliente, "lock health/armor" en clones e interpolar con `SetTargetPosition` durante `PED_SYNC_RATE`.

### Cómo evita que el juego borre/genere (`Client/multiplayer_sa/CMultiplayerSA.cpp`)
MTA apaga toda la población y la policía: `RemovePed` 0x610F20 → ret, `AddToPopulation` 0x614720 → ret, `ManagePed` 0x611FC0 → ret, `CPopulation::Update` 0x616698 → corta, `RemoveDistantCars` 0x42CD10 → ret, `GenerateRandomCars` 0x4341C0 → ret, `GenerateOneEmergencyServicesCar` 0x42B7D0 → ret, `GenerateRoadBlocks` 0x4629E0 → ret, car generators 0x6F3F40 → ret, aviones 0x6CD2F0 → ret, wanted 0x60EBCC/0x53BFF6 NOP, helis 0x561FA4 → 0, `CCrime::ReportCrime` 0x532010 → ret, `StreamVehiclesAndPeds` 0x40E7DF → salto.
**Policía:** MTA no sincroniza IA policial; el wanted es solo HUD. No resuelve "policía por jugador"; sirve el modelo de syncer.

## 2. gta-reversed — gta-reversed/gta-reversed
Funciones clave (`RH_ScopedInstall` en `source/game_sa/*.cpp`):
| Función | Dirección | Por qué sirve |
|---|---|---|
| `CPopulation::Update` | 0x616650 | punto único para decidir "este cliente genera o no"; `ManagePopulation` usa solo el centro del jugador local |
| `CPopulation::ManagePed` | 0x611FC0 | **donde enganchar el handoff**; su `RemovePed` está en 0x612227; `GetIsOnScreen` en 0x612157/0x6121C4 |
| `CPopulation::RemoveAllRandomPeds` | 0x6122C0 | borra todos los random al entrar/salir de interior |
| `CPopulation::AddToPopulation` | 0x614720 | elige tipo/posición vía `CPathFind::GeneratePedCreationCoors` (0x44E790, call 0x614B1C) → gatear por sector |
| `CPopulation::AddPedAtAttractor` | 0x614210 | ped en banco/tienda (call 0x615C0D) |
| `CPopulation::PopulateInterior` | 0x616470 | peds del interior al entrar; evitar duplicados |
| `CCarCtrl::GenerateOneRandomCar` | 0x430050 | genera autos; `GenerateCarCreationCoors2` 0x424210 (call 0x430455) → gatear por sector |
| `CCarCtrl::PossiblyRemoveVehicle` | 0x424F80 | **donde enganchar el handoff de autos**; Remove inline en 0x4251F6/0x425231/0x42542E |
| `CCarCtrl::RemoveDistantCars` | 0x42CD10 | borra por distancia al centro local |
| `CWanted::Update` / `ReportCrimeNow` | 0x562C90 / 0x562120 | el wanted corre solo para el jugador local |
| `CTaskComplexPolicePursuit::SetPursuit` | 0x68BBD0 | elige el jugador más cercano de `CWorld::Players`, pero se registra en `FindPlayerWanted()` local |
| `FindPlayerPed/Wanted/Coors/CentreOfWorld` | 0x56E210 / 0x56E230 / 0x56E010 / 0x56E250 | `-1` = `CWorld::PlayerInFocus` |

**Policía:** casi todo usa `FindPlayerWanted(-1)` = jugador local. `SetPursuit` recorre los 2 slots y toma el más cercano, pero el registro y la condición de seguir son del wanted local. Conclusión: cada cliente debe correr la policía de **su** jugador; esos cops se sincronizan como entidades normales y no se traspasan mientras persiguen. Para que un cop persiga a un jugador remoto habría que hookear `FindPlayerWanted`/`SetPursuit` (0x68BBD0, 0x66B160, 0x68FD50); ningún proyecto open-source lo hace.

## 3. koltrbtw/CoopAndreas (el más útil)
Ramas: `feature/zone-streaming`, `zone-radial-population`, `world-streaming-radius`, `host-for-missions-only`, "ownership handoff of ambient peds and vehicles", `vehicle-tow-ownership`, `mission-vehicle-ownership`, `air-population-sync`, `player-group-ownership`, `shared-wanted-level`, `traffic-lights-sync`. Se inspira en GTA V (`NetObjProximityMigrateable`).
- **Sectores (server):** grilla 60×60 de 100 u (origen −3000); cada jugador reclama ±200 u (≤25 sectores), first come first served, libera a los 100 ms. Celda = `areaCode × SECTOR_COUNT + sector` (los interiores tienen su grilla). Update cada 250 ms; manda `WORLD_GRID` solo si cambió.
- **Spawn gateado (cliente):** hookea call sites: 0x430455 y 0x42B86A (`GenerateCarCreationCoors2`), 0x614B1C (`GeneratePedCreationCoors`), 0x615C0D (attractors), 0x43E983 (`PopulateInterior`), 0x61679B/0x615D4B (`AddToPopulation`), 0x594D48/0x594FD7/0x59676F (staff de tiendas), 0x6F3EC1 (car generators), 0x53BFE2 (helis), 0x434272 (aviones). `CanSpawnAt(pos)` = offline o `currArea == áreaPropia && sector(pos)` es mío; `CanPopulateInterior` no puebla si otro jugador ya está a <`INTERIOR_RADIUS`.
- **Población radial:** `GetIsOnScreen → true` en las decisiones de borrado (0x425057/0x4252B3/0x4253B1, 0x612157, 0x43199F) y `0x425109 = 0xEB` (no reduce a 70 u al mirar al piso). Sin esto, que el dueño mire al piso borra el tráfico de todos.
- **Handoff:** `PED_HANDOVER_DISTANCE = 50`, `VEHICLE_HANDOVER_DISTANCE = 120`, tren 200, hold 10 s. Hook del `RemovePed` de `ManagePed` y de los 3 `CWorld::Remove` inline de `PossiblyRemoveVehicle`; también `RemoveAllRandomPeds`. Criterio: remoto a <50/120 **y** `distTarget² < distOwner² · 0.8`, y 10 s desde el último cambio. No traspasa peds en vehículo/muriendo/muertos/en grupo, autos con el jugador local, remolques ni trenes. Migración periódica con hold 3 s, remoto <50 y dueño >4.
- **Server valida:** solo acepta del syncer actual; si el target se fue, vuelve al viejo; `EnsureRelevant` manda el spawn al nuevo si no tenía clon y reestampa `serverTime`. `CRelevanceManager`: scope enter 300 / leave 350 (histéresis); ambientales solo para cercanos, misión para todos.
- **Wanted:** manda el nivel propio y **sube al máximo del grupo** (cadena de vecinos a <`GROUP_RADIUS`) si uno ya tiene estrellas. ⚠️ **Contradice nuestra regla**: copiar solo el envío del nivel, no el `SetWantedLevelNoDrop` grupal. Para policía propia, no gatear `GenerateOneEmergencyServicesCar` (0x42B86A) por sector sino por "el jugador local tiene wanted".

## 4. Otros
- **itsLighty/CoopAndreas:** 206 commits por delante. Ver "Synchronize EnEx enter and exit transitions", "preserve EnEx area", "fix duplication of peds at attractors", "authoritative mission session protocol", "authoritative cutscene skip voting".
- **lautaroschenfeld/CoopAndreas-DE:** mezcla koltrbtw + itsLighty; "Handle a full ped pool", "Skip the ped group of network players", "Guard EnEx doors". ⚠️ Comparte dinero de muertos: no copiar.
- **nullptr03/CoopAndreas:** suciedad de autos y movimiento de cabeza (menor).
- **open.mp / SA-MP:** desactivan población; NPC server-side. No aportan al modelo syncer/handoff ni a policía.

## 5. Resumen aplicable
1. Syncer por cercanía con histéresis (conservar <R, reasignar <R−20; ped 100, auto 130), revisar cada 500 ms; conductor jugador = syncer del auto; desempate por menor carga.
2. Handoff iniciado por el dueño al borrar; server valida, `EnsureRelevant`, reenvía con `serverTime` nuevo; cooldown 3–10 s.
3. Contexto de sync por entidad para descartar paquetes del dueño viejo.
4. Población por sectores de 100 u, reclamo ±200, por `areaCode`; gatear call sites; población radial.
5. Policía por jugador: no gatear por sector sino por wanted local; los cops generados son del buscado y no migran mientras persiguen; wanted individual (solo informar el nivel).
6. No apagar toda la población como MTA salvo que se quiera todo server-side.
