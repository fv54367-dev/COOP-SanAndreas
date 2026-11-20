# Tanda 2026-11-14 (Sonnet) - Sincronizar barcos

Rama `tanda/2026-11-14-barcos`. Sin cambio de protocolo (ningún paquete nuevo ni tocado; server y testbot intactos).

## Qué fallaba (por lectura de código, sin probar en juego)
Los barcos viajan como cualquier vehículo (VehicleSpawn / DriverUpdate / IdleUpdate, copia `CBoat` en `CNetworkVehicle`). La copia es STATUS_ABANDONED y:
1. `CBoat` nace con `bAnchored` y solo STATUS_PLAYER/SIMPLE/PHYSICS lo sueltan (gta-reversed `Boat.cpp`). La copia quedaba anclada: velocidad x/y en 0 cada frame y rumbo forzado al inicial, sin estela, giros a tirones.
2. En ABANDONED `ProcessControl` pone timón 0, gas 0 y deja caer la hélice: el conductor remoto se veía sin timón ni motor.
No se tocó: aparición, pasajeros, posición/rotación (ya suavizadas por `smooth-vehicles`).

## Qué se hizo
`client/src/Hooks/VehicleHooks.cpp`: `FreeRemoteBoat` (suelta el ancla si hay conductor o velocidad > 0.02) en la copia con conductor jugador y con conductor NPC / sin conductor; `ApplyRemoteBoatInputs` (tras la física, repite `ProcessControlInputs` 0x6F0A10 con el mando remoto del conductor y la ley de la hélice). Las copias que este juego hospeda (`m_bSyncing`) no se tocan.

## Interruptor
Cliente `[config]` `remote-boat-sync=true` (en `INTERRUPTORES.md`). `false` = como antes.

## Pruebas simples (SIN PROBAR)
- [ ] Barcos 1: A maneja un Dinghy/Speeder; B lo ve avanzar suave, con estela y sin volver al rumbo inicial.
- [ ] Barcos 2: B ve el timón girar y la hélice acelerar/frenar según A.
- [ ] Barcos 3: pasajero en un barco: viaja con él sin tironear.
- [ ] Barcos 4: barco de NPC (muelle / Predator) visto desde el otro: se mueve con estela.
- [ ] Barcos 5: barco vacío anclado en el muelle sigue quieto en ambos.
- [ ] Barcos 6: `remote-boat-sync=false` vuelve al comportamiento anterior.

## No pude
Probar en juego (no se instaló ni abrió). El conductor NPC no muestra timón/hélice (solo se libera el ancla). Posible que el gas por mando remoto no coincida si el pad 0 no está en contexto remoto en ese punto: si el timón no se mueve, mirar `ApplyRemoteBoatInputs`.
