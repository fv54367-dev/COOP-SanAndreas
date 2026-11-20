# Tarea T57: GPS con la ruta al punto del mapa (activable en F1)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`. No toques `main.scm` ni `scm/`.

## Pedido del dueño
Mientras manejás, el radar muestra la ruta por calles hasta el punto que marcaste en el mapa. **Se prende y se apaga desde el menú F1** (`client/src/UI/CModeMenu.cpp`).

## Referencia (MIT)
`_refs\sa-coop-karimov\src\client\features\waypoint\` (`Gps.cpp`): el hook de `CRadar::DrawBlips` dibuja la ruta antes de los blips (queda debajo); la ruta sale de los nodos de calles del propio juego (`CPathFind`, mirá `_refs\gta-reversed` para la búsqueda de ruta y el dibujo del radar). También dibujan los puntos de mapa de los otros jugadores.

## Cambio
- Opción nueva en F1: "GPS: sí/no" (por jugador, guardada en `UserFiles\coopandreas.ini` como `gps-route`, default **true**; documentada en `INTERRUPTORES.md`). Apagado: radar exacto de hoy.
- Ruta recalculada al cambiar de punto o al salirse de ella (sin recalcular cada frame: no tiene que bajar los FPS).
- Solo en vehículo de calle (no en avión, barco, ni a pie, salvo que sea trivial).
- Ver los puntos de mapa de los otros jugadores: solo si ya viajan por red; si requiere protocolo, hacelo con versión subida y su propio interruptor `gps-share-waypoints`, o dejalo fuera y decilo.

## Casos borde
- Sin punto marcado, en interior o en misión que oculta el radar: no dibuja nada.
- Punto en una isla sin conexión por calle (o ciudades bloqueadas): ruta hasta el nodo alcanzable más cercano o nada, sin colgarse.
- Radar en zoom distinto / mapa grande del menú: no rompe el dibujo.

## Aceptación
Compilan client y server. CRLF. Cita MIT. `TESTEAR_HOY.md`, `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.
