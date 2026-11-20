# TANDA 2026-11-12 (noche) — Claude: abiertos de ESTADO.md

Objetivo: atacar los "Bugs abiertos" y "Qué sigue" de `ESTADO.md` y mejorar la sync visual (que lo que ve un jugador lo vea el otro). Escriben subagentes Sonnet, cada uno en su worktree; Claude revisa, mergea, sube versión de protocolo y corre el testbot.

## Puntos y archivos (nadie toca los archivos de otro punto)
- **A. NPC a pie sin saltos (protocolo 0.4.47):** `shared/network/packets/peds.h` (PedOnFoot: velocidad), `client/src/PacketHandlers/peds.cpp` (PED_ONFOOT), `client/src/CNetworkPedManager.cpp`, `client/src/CNetworkPed.*`, `client/src/CNetworkSmoothing.h`, `CConfigLoader.*` (interruptor `npc-velocity-sync`), `shared/config.h`.
- **B. Animaciones del jugador remoto (protocolo 0.4.48):** paquete nuevo al final de `packet_types.h` + `shared/network/packets/players.h`, `client/src/CPlayerAnimMirror.*` (nuevo), `client/src/PacketHandlers/players.cpp`, `server/src/PacketHandlers/players.cpp`, `CConfigLoader.*` (interruptor `player-anim-mirror`), registro en `Main.cpp`/`CCore.cpp` (una línea).
- **C. Telemetría de memoria/FPS y armas sin KEEP_IN_MEMORY:** `client/src/CUtil.cpp` (GiveWeaponByPacket), `client/src/CCrashLog.cpp` o `CPerfWatch.*` (nuevo), `CConfigLoader.*` (interruptores `perf-telemetry`, `weapon-model-release`).
- **D. Máquinas de misión fase 3 (Cop Wheels, cabrestante del Leviathan, grúa de Customs Fast Track):** `client/src/CMissionMachines.*`, `shared/network/packets/mission_machines.h` (si hace falta: protocolo lo integra Claude).
- **E. Spawn-watch (Claude):** `client/src/CSpawnWatch.cpp`.

`CConfigLoader.*`, `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md` y `shared/config.h` los tocan varios: en el merge Claude resuelve (cada uno agrega su bloque al final, sin reordenar).

## Restricciones
- CRLF en `.cpp/.h` (Python `newline=''`), nunca `sed -i`. Estilo del código vecino, comentarios en inglés cortos.
- Todo sistema nuevo con interruptor (apagado = comportamiento anterior) y documentado en `INTERRUPTORES.md`.
- gta-reversed (`E:\juegos\gtasacoopnuevo\_refs\gta-reversed`) para cada función/offset; citarlo en el commit.
- Un commit por punto, en español, prefijo `feat:/fix:/net:`.

## Casos borde (todos los puntos)
- 3–4 jugadores: nada debe asumir "el otro jugador"; todo por jugador/ped.
- F1 individual: la sync visual (A, B) sigue igual (es mundo, no misión); D solo en misión coop.
- Desconexión/muerte a mitad: B corta la animación de la copia; D libera la máquina/objeto.
- Interior u otra punta del mapa: B no aplica si la copia está en otra área o fuera de streaming.
- Cinemáticas: B no aplica durante cinemática ni a copias poseídas por `CScenePartner`/`CCutscenePartners`.
- Interruptor apagado: formato de red y comportamiento viejos.

## Hallazgo E (Claude, evidencia logs 2026-11-12)
Los `dup-ped` de la última sesión (J1: 122× modelo 106, 18× 280; J2: 47× 107, 34× 7) son todos `sync=1/1`: dos peds del MISMO cliente (grupos de pandilla y parejas de policías nativos), no duplicados entre clientes. `cop-watch` sin copias ni locales. No hay evidencia de duplicados coop en esa sesión; el log se cambia para marcar solo pares dueño/copia.

## D: resultado (subagente Sonnet, rama propia; Claude revisa)

**Cop Wheels (HEIST4): hecha en C++, SIN PROBAR.** Sin paquete nuevo ni cambio de protocolo; interruptor `coop-mission-machines` (+ `mission-guest-counts`, que ya existe).
- Evidencia del script (`HEIST4.txt`, mision `HEIST4` en main.scm: inicio 0x246B1D, 13637 bytes, huella FNV-1a 0x68971E7B, verificada por `tools/inspect_d_heist4.py`): la carga es el bucle 5805-6000: `Char.IsInCar($scplayer, 279@(48@,4i))` (876, opcode 00DB, offset 5939) -> estado 266@=1 -> `Char.IsInAngledAreaInCar3D($scplayer, rampa, 4.8)` (931, opcode 05FE, offset 6340, 9 parametros) -> velocidades -> `Car.AttachToCar(moto, Packer)` (943, 0683) + `Car.FreezePosition` + `273@ += 1` (contador de motos, una sola variable del script del dueño) + `Player.SetControl($player1, False)` 4 s con camara en el camion (968-971). Lo unico que ata el chequeo al CJ del dueño es `$scplayer` en esas dos condiciones.
- Cambio: dos spots nuevos en `CMissionGuestCounts` (misma tecnica de MANSIO2/MAF4: nombre + IP + huella del bloque, recomprobada en cada uso) que, si el dueño no esta en la moto/area, prueban los mismos opcodes con cada invitado elegible (`COpCodeSync::ProbeCondition`, ahora acepta 00DB y 05FE; params del cliente 8 -> 10). El script engancha la moto solo: el CJ del dueño no se mueve ni sube a nada. 3-4 jugadores: se prueba a cada invitado elegible (mismo area, no F1 individual, vivo, no abandono); dos invitados con motos distintas cuentan las dos (el bucle recorre las 4 motos).
- `ATTACH_CAR_TO_CAR` (0683) entra a la tabla de opcodes sincronizados pero `IsOpcodeSyncable` solo lo deja pasar en HEIST4 con el interruptor encendido; asi el invitado ve la moto en el camion. El invitado se baja (`WARP_CHAR_FROM_CAR_TO_COORD` 0x0362, 4 m a la derecha del Packer sobre el suelo, `GuestBikeLoaded` en `CMissionMachines`) cuando `CVehicle::m_pAttachedTo` de su moto es un Packer (443): evita que siga conduciendo (y sincronizando) una moto atada. gta-reversed: `CVehicle`/`CPhysical::m_pAttachedTo` (Physical.h 133), CommandHandlerTable (el mismo despacho que ya usa `ProbeCondition`).
- Desconexion/muerte/F1: `Eligible` descarta al invitado; si la moto no llego a engancharse el script vuelve al estado 0 (rama 6819) y pide la moto como siempre. Interruptor apagado: los spots no se buscan, el attach no se replica: como antes.
- **Supuesto sin confirmar**: que un invitado pueda subirse como conductor a una moto `MISSION_VEHICLE` del dueño y que el dueño lo vea dentro (`ped->m_pVehicle`). Si no puede subirse, la prueba D1 lo mostrara y no hay nada mas que hacer aqui (es regla de vehiculos de mision, otro tema). Tampoco se cubre: motos que el script crea solo si el CJ del dueño esta a menos de 200 m (HEIST4.txt 1077).

**Up, Up and Away! (HEIST5, cabrestante del Leviathan): NO hecha, evidencia insuficiente.**
- El cabrestante es una `CRope` por vehiculo: `Heli.AttachWinch` (0788) pone `CVehicle::m_nWinchType` (InitWinch 0x6D3B60), `UpdateWinch` (0x6D3B80, gta-reversed Vehicle.cpp 2520) registra la cuerda cada cuadro con `CRopes::RegisterRope`, la altura es `Get/SetRopeHeightForHeli` (0x6D3D10/0x6D3D30) y el enganche `PickUpEntityWithWinch` (0x6D3CD0) / `GrabEntityOnWinch` (078B, `HEIST5.txt` 1190/1380, es una consulta que se repite cada cuadro, no una accion). Nada de eso esta sincronizado: la copia del Leviathan de los invitados no tiene cuerda. El furgon (122@) es un vehiculo de mision del dueño con sync de posicion normal, asi que los invitados ya deberian verlo moverse con el helicoptero; lo que falta es la cuerda/gancho.
- Diseño que queda listo para hacer: el dueño (script HEIST5, Leviathan 34@ con `m_nWinchType != 0`) manda tipo + altura (`GetRopeHeightForHeli`) + id del vehiculo enganchado (`QueryPickedUpEntityWithWinch`) a ~10 Hz; la copia aplica `InitWinch(tipo)`, `SetRopeHeightForHeli(h)` y llama `UpdateWinch()` por cuadro, SIN `PickUpEntityWithWinch` (el furgon ya lo posiciona el sync; recoger la copia pelearia con el). Falta evidencia que solo da el juego: quien llama `UpdateWinch` en un heli sin jugador (los llamadores estan en el exe sin reversar) y si la cuerda de la copia se dibuja. Mezclarlo en `MACHINE_PHASE` ensuciaria el puesto de la torreta (HEIST5 usa TURRET antes); lo prolijo es un paquete propio `WINCH_STATE` (protocolo, lo integra Claude). Un invitado que pilote y enganche queda descartado: `Char.IsInCar($scplayer, 34@)` (1168/1378) exige al dueño dentro.

**Customs Fast Track (STEAL4, grua): NO hecha, evidencia insuficiente.**
- `Crane.EnableControls` (STEAL4.txt, 25 usos desde la linea 734) activa `CRopes::PlayerControlsCrane` (gta-reversed Ropes.h 25, `eControlledCrane::MAGNO_CRANE`, 0xB76898); la grua del puerto es un conjunto de `CRope` simulado y controlado por el pad en cada juego. gta-reversed no tiene reversadas las clases de la grua (`Cranes.cpp` son 41 lineas de stubs) ni los campos de `CRope` usados al controlarla; sin eso, copiar el estado de las cuerdas del dueño a los invitados serian offsets sin verificar.
- Falta: (1) volcar con el juego abierto las `CRopes::aRopes` activas durante la grua (tipo, altura, `m_pRopeAttachObject`) y los campos de la grua del puerto; (2) paquete dueño->invitados con esos campos; (3) los autos colgados ya son vehiculos del dueño con sync de posicion (se ven moverse), asi que el minimo "ver la grua y el auto colgado" depende solo de (1)-(2). Pruebas D2/D3 no se agregan: no hay nada que probar.

Build: client, server y testbot compilan (ver abajo); testbot TODO PASS lo corre Claude.

