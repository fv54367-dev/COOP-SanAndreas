# Tarea T78: `/tp <jugador>` también en modo prueba

Leé `AGENTS.md` y `tareas\COMUN_reglas.md`. Código: `server/src/PacketHandlers/players.cpp` (comandos de chat, `test-commands`), lo de T60 (`SOCIAL_ACTION`, `tp-request`) en client y server.

El dueño prueba con `PROBAR_SOLO.bat` (`test-commands=1`): `/tp Jugador2` no hace nada, `/tp` solo acepta coordenadas. Pedido: en modo prueba, `/tp <nombre>` (y `/ir <nombre>`) te lleva al lado de ese jugador **sin preguntar**, igual que `/traer`; `/tp x y [z]` sigue igual. Fuera del modo prueba no cambia nada (pedido Y/N de T60). Nombre: sin distinguir mayúsculas, y prefijo único (`/tp jug2` no, `/tp Jugador2` sí; si hay ambigüedad, mensaje con los candidatos). Destino desconocido, uno mismo, o destino en otro interior: mensaje claro (si está en interior, llevarlo con su área como ya hace `/traer`). Actualizá `/ayuda` y `TESTEAR_HOY.md` (T60). Sin protocolo nuevo si se puede.

## Aceptación
Compilan client/server/testbot, `tools\correr_testbot.bat` TODO PASS, CRLF. No instales. Resumen ≤10 líneas.
