# T90 (sonnet): garajes sincronizados

Protocolo 0.4.42-alpha, interruptor `garage-sync` (default true), ver INTERRUPTORES.md.

## Diseno
- Cliente `CGarageSync::Process` (cada frame): para cada garaje de script lee `m_nDoorState` (0x4D); si cambia entre abierto (OPEN/OPENING/WAITING) y cerrado respecto de lo ultimo visto/aplicado, manda `GARAGE_DOOR {indice, hash FNV del nombre, abierto}`.
- Servidor: guarda el ultimo estado por indice y lo reenvia a TODOS los jugadores (emisor incluido, no individuales excluidos); al entrar un jugador se le reenvia el estado guardado. Si queda solo uno, se borra el estado.
- Receptor: guarda el estado pendiente y en `Process` hace lo de `CGarage::OpenThisGarage/CloseThisGarage` (gta-reversed Garage.cpp: solo cambia el estado de la puerta; `CGarage::Update` la desliza, sin teletransportar). Si el hash del nombre no coincide con el garaje de ese indice, se ignora. Despues re-lee el estado real para no devolverlo como cambio propio (sin eco).

## Que se sincroniza
Tipos de script: ONLY_TARGET_VEH 1, OPEN_FOR_TARGET_FREEZE_PLAYER 14, SCRIPT_ONLY_OPEN 15, SCRIPT_CONTROLLED 19, STAY_OPEN_WITH_CAR_INSIDE 20, CLOSE_WITH_CAR_DONT_OPEN_AGAIN 21, SCRIPT_OPEN_FREEZE_WHEN_CLOSING 23.
No: bombas (2-4), Pay 'n' Spray (5), tuneo (36-38), casas (16-18, 24-32, 39-42), depositos (33-35), burglary 43, hangares 44-45: reaccionan al jugador local o guardan autos; sincronizar la puerta rompe su logica (cobrar/pintar en el juego equivocado). Karimov tambien comparte esos con un arbitraje por cercania; se dejo fuera por riesgo.

## Casos borde
- Dos a la vez: el servidor impone un orden unico y todos lo reciben, incluido el emisor; todos terminan con el ultimo.
- Garaje en otro interior: los garajes de script son exteriores; no se filtra por area.
- Desconexion del dueno: no hay dueno; el estado guardado queda y lo ve quien entra.
- 3-4 jugadores: reenvio a todos, sin cambios.
- Interruptor apagado, F1 individual, offline: no se envia ni se aplica; el estado se reaprende al volver (el primer estado visto no cuenta como cambio).
- Limite: si cada cliente corre el mismo script, cada uno puede enviar el mismo cambio (inocuo, idempotente). Garajes cuyo indice/nombre difieren entre partidas se ignoran.

## Citas
Karimov-Akbar/sa-coop (MIT) `src/client/features/garages/Garages.cpp` (ScriptDoor/DoorShared, estados de puerta); entrada en NOTICE.md. gta-reversed `Garage.cpp`/`Garage.h` (OpenThisGarage, CloseThisGarage, eGarageDoorState). No se uso _refs\sacoop.

## Pruebas
Compilan client, server y testbot. Escenario (n) del testbot (relay, individual, tardio) escrito pero NO corrido. Prueba en juego: TESTEAR_HOY.md.
