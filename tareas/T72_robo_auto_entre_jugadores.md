# Tarea T72: robo de auto entre jugadores, la víctima sale teletransportada
Leé `tareas\COMUN_reglas.md`, `AGENTS.md` y la parte T51 (`player-jack-native`) de `tareas\TANDA_2026-11-10_sync_visual.md` y de su commit (`git log --grep=T51`, `9c85f1a`, `519c91f`).

## Reporte del dueño (prueba en juego, 2026-11-10)
"Robo de vehículo entre jugadores: el invitado le saca el auto al host y al revés → **se tpean ahora afuera al jugador que lo quieren sacar del vehículo**." Esperado: a la víctima la sacan por la puerta con la animación del juego (como a un NPC), cae y se levanta, sin saltos, en las dos pantallas. Probar también de acompañante.

## Qué hacer
1. Encontrá la causa con evidencia: qué camino corre cuando el que roba es un jugador de red y la víctima es el jugador local (y al revés), quién saca a quién en cada juego, y qué termina llamando a un warp/`SetPosn`/`RemoveFromVehicle` en vez de la tarea nativa de ser sacado (`CTaskComplexCarSlowBeDraggedOut`, `CTaskSimpleCarJumpOut`/`CTaskComplexDragPedFromCar`: buscá en `_refs\gta-reversed\source\game_sa\Tasks\...`). Mirá también los handlers de VEHICLE_ENTER/EXIT, `CJackSync`, `CCarEnterExit` y el sync de pasajero/conductor, por si una actualización de posición de red pisa la animación (por ejemplo, la víctima remota recibe una posición "afuera" antes de que termine la tarea).
2. Arreglá la causa (no el síntoma), detrás de `player-jack-native` (apagado = como antes de T51).
3. Escribí la causa (con `archivo:línea` y la función de gta-reversed) en `tareas\TANDA_2026-11-10_sync_visual.md`, sección T72.

## Casos borde
Conductor y acompañante; 3–4 jugadores (otro mirando); víctima en misión coop (no debe fallar la misión por el robo); la víctima se desconecta en medio; el ladrón muere en medio; moto y bici (otras tareas de salida).

## Aceptación
Compilan client/server/testbot; `tools\correr_testbot.bat` TODO PASS (si hay protocolo: versión siguiente a la de `shared/config.h`). CRLF. Reemplazá la línea "[N] Robo de vehículo entre jugadores" de `TESTEAR_HOY.md` por la prueba nueva (`[ ]`). No instales. Resumen ≤10 líneas.
