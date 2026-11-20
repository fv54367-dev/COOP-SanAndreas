# Tarea T55: billar de a dos por red
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`. No toques `main.scm` ni `scm/`.

## Pedido del dueño
Jugar al billar del bar (`POOL2`) entre dos jugadores reales. Estaba descartado; el dueño lo retoma (2026-11-10).

## Referencia (MIT)
`_refs\sa-coop-karimov\src\client\features\story\pool\` (Pool.cpp/.h): `POOL2` tiene un modo de dos jugadores propio: el segundo pad de la consola se une en la pantalla de apuesta (START en él: `SET_SCRIPT_COOP_GAME`, `178@ = 1`) y los turnos van por el pad en `180@` (0 el jugador del juego, 1 el segundo). Leé cómo mapean el pad remoto, las bolas y el resultado.

## Lo que ya hay acá
`client/src/CNetworkPadSync.cpp` ya tiene `ms_aszTwoPlayerScripts[] = {"POOL2"}` (intento anterior). Investigá qué hace y por qué quedó descartado (`git log -S POOL2`, `ESTADO.md`) antes de diseñar; reusá lo que sirva.

## Cambio
Interruptor `pool-two-players` (cliente, default **true**, en `INTERRUPTORES.md`). Apagado: como hoy.
- Uno arranca el billar; otro jugador cerca de la mesa puede unirse en la pantalla de apuesta (tecla del juego o la que usen ellos; mostrar ayuda en pantalla).
- El juego corre en quien lo arrancó; el que se unió ve la mesa, el taco y las bolas igual y juega su turno con sus teclas.
- Apuestas: cada uno paga y cobra de su propio dinero (regla). Si no se puede separar, sin apuesta en el modo de dos y decilo.

## Casos borde
- Tercer y cuarto jugador: solo miran (POOL2 es de dos).
- El que se unió se va o se desconecta: el billar vuelve a uno contra la IA o termina limpio, sin colgar el script.
- Interruptor apagado / jugador en individual: como hoy.

## Aceptación
Compilan client/server/testbot (versión si hay protocolo). CRLF. Cita MIT. `TESTEAR_HOY.md`, `INTERRUPTORES.md`, sacá el billar de "Descartado, no retomar" en `ESTADO.md`, resumen en la tanda. No instales. Resumen ≤10 líneas.
