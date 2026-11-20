# Diseño B / C / D — traspaso, zonas, policía e interiores

> Diseño original (2026-10-19), ya implementado en las etapas B–D. Direcciones SA 1.0 US verificadas con capstone salvo las marcadas "(verificar)". Rutas relativas a `coopandreas-server\`.

## 0. Base del código
| Hecho | Dónde |
|---|---|
| Una entidad pasa a "hosteada" (dueño = este cliente) en `CWorld::Add` | `client/src/Hooks/WorldHooks.cpp` → `CNetworkPed/Vehicle::CreateHosted` |
| Cuando el juego del dueño la borra, `CWorld::Remove` hookeado hace `delete` del record → `PED_REMOVE`/`VEHICLE_REMOVE` a todos | `WorldHooks.cpp`, `CNetworkPed.cpp`, `CNetworkVehicle.cpp` |
| Las copias remotas se crean como `MISSION_CHAR` (createdBy=2): el juego no las administra ni las borra | `CNetworkPed.cpp`, `PacketHandlers/peds.cpp` |
| El server es relay; ya valida que solo el `m_pSyncer` mande updates y tiene claims que reasignan al borrar | `server/src/PacketHandlers/peds.cpp` |
| Al desconectar se borra todo lo hosteado | `server/src/CNetwork.cpp` → managers |
| El server recibe posición+interior del jugador pero no los guarda | `shared/network/packets/players.h`, `players.cpp` |
| `WorldPositionCompressed` hereda de `CVector` (el server lo lee directo) | `shared/network/serializable_types.h` |

Call-sites útiles (verificados):
| Función | Dirección | Call-site |
|---|---|---|
| `CPopulation::Update` | 0x616650 | 0x53C030 / 0x53C054 (ya hookeado) |
| `CPopulation::ManagePed` | 0x611FC0 | único `RemovePed` en **0x612227**; `field_54C` (keep-alive) en 0x612162 |
| `CPopulation::RemovePed` | 0x610F20 | InteriorGroup_c: 0x594E59, 0x595088, 0x5952E6 |
| `CPopulation::AddToPopulation` | 0x614720 | `CPathFind::GeneratePedCreationCoors` (0x44E790) en **0x614B1C** |
| `CPopulation::AddPedAtAttractor` | 0x614210 | 0x615C0D (dentro de `GeneratePedsAtAttractors`) |
| `CCarCtrl::GenerateOneRandomCar` | 0x430050 | 0x434263/0x434268 (hookeados) y **0x434251** (ráfaga inicial, sin hook) |
| `CCarCtrl::GenerateCarCreationCoors2` | 0x424210 | **0x430455** |
| `CCarCtrl::RemoveDistantCars` | 0x42CD10 | `PossiblyRemoveVehicle` 0x424F80; borra en 0x4251E6/0x425221/0x42541E |
| `CCarCtrl::RemoveCarsIfThePoolGetsFull` | 0x4322B0 | 0x53C1D0 |
| `CRoadBlocks::GenerateRoadBlocks` / `CHeli::UpdateHelis` | 0x4629E0 / 0x6C79A0 | 0x53C1C6 / 0x53BFE2 (sin filtro coop) |

## B) Traspaso de NPCs y autos ambiente
**Causa:** cualquier `CWorld::Remove` de una entidad hosteada manda `PED_REMOVE`/`VEHICLE_REMOVE` a todos. No se distingue "lo borro porque está lejos de mí" de "murió / lo borró un script".

**Diseño:** liberar = "yo lo saco de mi mundo, que otro lo hostee". El dueño destruye su entidad local pero conserva el record como copia remota streamed-out y avisa al server; si nadie lo quiere, recién ahí se manda el REMOVE a todos.

1. **Server (`CWorldTracker`):** guarda posición/área de cada jugador (on foot, conductor, pasajero) y de cada ped/auto; jugador sin posición por >5 s no es candidato. Tick cada 250 ms.
2. **Protocolo:** `PED_RELEASE {pedid, pos, heading, health, areaCode, vehicleid?, seat, reason}` y `VEHICLE_RELEASE {vehicleid, pos, rot, velocity, health, pedids[≤9]}`. Se agregan al final de `packet_types.h`.
3. **Cliente dueño:** envuelve el call-site 0x612227 y las banderas de `RemoveDistantCars` / `RemoveCarsIfThePoolGetsFull` / `RemovePedsIfThePoolGetsFull`. Si es ambiente, está vivo y no es policía persiguiendo: manda RELEASE, `CleanUpOldReference`, `m_pPed=nullptr`, `m_bSyncing=false`, `m_bStreamedOut=true`, `m_bReleasePending=true`. Para autos, libera también a los ocupantes hosteados.
4. **Server al recibir RELEASE** (solo del syncer): primero los claims; si no, candidato = jugador ≠ emisor, misma área, el más cercano dentro de `handover-ped-radius` (100 m) / `handover-vehicle-radius` (200 m; aeronaves 400); empate → id menor. Asigna y manda `ASSIGN_PED{pos, heading, takeover}` (autos: `ASSIGN_VEHICLE` + cada ocupante, atómico). Nadie → REMOVE a todos.
5. **Nuevo dueño:** si el record está streamed-out, `StreamIn()`; si falla (pool lleno / >150 m) responde REJECT. Al quedar creado: `SetCharCreatedBy(RANDOM_CHAR)`, restaura decision maker/rangos originales, `field_54C = now+10 s`, y tarea ambiente: a pie `Wander` (0x673D00); conductor `MISSION_CRUISE` + `JoinCarWithRoadAccordingToMission` (0x432CB0) + `DRIVE_WANDER`; pasajero nada; en interior ver D.
6. **Desconexión:** para ambiente, `HandoverAllHosted` con la última posición conocida; misión y autos con jugadores → comportamiento actual.

**Casos:** doble dueño imposible (el server solo reenvía al syncer); ASSIGN idempotente; REMOVE de quien ya no es syncer se ignora; ping-pong limitado por radios (candidato < radio de borrado), keep-alive 10 s y rechazo de un segundo traspaso antes de 5 s; el record liberado se re-crea si el emisor se acerca (`CStreamingSync`).
**Interruptor:** server `handover=1` + radios; cliente `handover=true`. Cualquiera apagado → REMOVE como antes.

## C1) Población por zonas
**Causa:** `CStreamingSync::IsOtherPlayerGeneratingNearby`: si hay un jugador de id menor a <300 m, este cliente no genera nada. Todo el tráfico vive alrededor del de id menor y a 300 m cambia de golpe.

**Diseño:** `IsPointMine(p)`: para cada jugador remoto con ped válido y misma área, es mío si `d_mio < d_otro` (empate → id menor); solo afuera (área 0), en interiores manda `CInteriorOwnership`.
- Wrappers en 0x614B1C, 0x615C0D y 0x430455: llamar al original y devolver `false` si el punto no es mío. También 0x434251 (ráfaga inicial).
- Con el interruptor encendido, `PedHooks` y `VehicleHooks` dejan de usar `IsOtherPlayerGeneratingNearby`: todos generan, cada uno en su mitad.
- **Densidad:** contar peds no-jugador (locales + copias) a <80 m en `CPopulation::Update`; si `>= zone-ped-budget` (25), `generate=false`. Para autos ya se pasa `numCarsInRadius` como `NumRandomCars`.
- Con B activo, lo generado en mi zona se traspasa al alejarme en vez de desaparecer.
**Riesgos:** duplicados en la frontera si la posición remota llega con lag (margen `d_mio < d_otro - 5 m`). Firma de 0x44E790 a confirmar.
**Interruptor:** cliente `zone-population=true`, `zone-ped-budget=25`. Apagado → regla de 300 m.

## C2) Policía por jugador
**Causa:** los patrulleros de persecución salen de `GenerateOneRandomCar`, que lee `FindPlayerWanted(-1)` (jugador local). A <300 m solo genera el de id menor, así que el otro con estrellas no genera policía; los patrulleros que ve son copias cuya IA corre en el juego del dueño y persiguen al dueño. Helis y roadblocks no estaban filtrados. El hook además ponía `NumLawEnforcerCars = 0`.

**Diseño:** cada jugador con estrellas genera sus patrulleros siempre, aunque otro esté cerca o el punto sea de otra zona; quedan hosteados por él y su IA lo persigue. Los demás los ven como copias. No se sincroniza el nivel.
- En `GenerateOneRandomCar`, si el filtro bloquearía pero el wanted local es >0, llamar al original con `s_bPoliceOnly`; un wrapper en `ChooseModel` 0x43022A devuelve `-1` para que solo salga el camino de policía; el filtro de zona se saltea.
- Reemplazar `NumLawEnforcerCars = 0` por el conteo real de patrulleros hosteados a <200 m.
- B: **no traspasar** cops ni autos de policía (perseguían a su dueño).
- Dos jugadores con estrellas juntos ven el doble de policías (esperado). Opcional `police-shared-cap` para bajar el tope local a la mitad.
**Interruptor:** cliente `police-per-player=true`.

## D) Interiores
**Causa 1 (invitados ven NPCs quietos):** el sync existente solo manda asociaciones de grupos de la lista blanca. Bailar, barra, casino, comer, fumar son anims con nombre (`m_nAnimGroup = -1`), así que `IsRelevantAnimation` da false. Además el índice del bloque se sacaba de `m_pAnimBlock`, que es nulo hasta que ese juego cargó el bloque; y el estado solo se mandaba al cambiar, no al entrar tarde.

**Diseño 1:** agregar al `PedTaskState` `namedAnim`, `block[16]`, `animHash`, `flags`, `time`, `speed`. El emisor toma la asociación de mayor blend sin grupo; el receptor pide el bloque (`RequestModel(25575 + idx)`, `GAME_REQUIRED`), reintenta en `Process` conservando el estado pendiente, y al cargar `AddAnimation` + `SetCurrentTime` + blend 4.0. Arreglar el índice por nombre (`GetAnimationBlockIndex(GetAnimBlockName(group))`). Reenviar el estado completo cada 3 s mientras `hasAnim` (peds a <60 m) y aplicarlo al recibir `PED_SPAWN`/stream-in.

**Causa 2:** al salir el dueño, su juego borra la población del interior y el invitado, 2 ticks después, hace `BecomeOwner(false)` + `RestoreHiddenPeds(true)`: aparecen sus NPCs locales.

**Diseño 2:** los call-sites de interior liberan (`PED_RELEASE` con `areaCode != 0`); candidato = jugador con misma área y <150 m. El nuevo dueño hostea y pone `PLAY_ANIM` en loop si la última anim era loop; si no, `StandStill` (el wander atraviesa paredes). No se re-lanzan cerebros. `BecomeOwner(false)` espera `INTERIOR_HANDOVER_GRACE = 3 s`; si llegó un takeover, los locales siguen ocultos hasta salir; si no, restaura como hoy.
**Interruptores:** `npc-named-anim-sync=true` (depende de `npc-task-sync`), `interior-handover=true` (requiere `handover` en ambos lados).

## Orden de implementación
1) D-anims → 2) B tracker + release/assign → 3) D-traspaso → 4) C1 zonas → 5) C2 policía. Cada interruptor documentado en `INTERRUPTORES.md`.
