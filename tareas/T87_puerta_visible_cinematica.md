# Tarea T87: la puerta del mapa sigue visible en la cinemática (T21 sin resolver)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md` y `tareas\T21_puerta_doble_driveThru_salida.txt` (el diagnóstico de T21, que quedó sin captura en vivo).

Código:
- `client/src/Hooks/GameHooks.cpp`
- `CObjectSync.cpp`
- gta-reversed: `CutsceneMgr.cpp` (`HideRequestedObjects`, `ms_crToHideItems`), `Object.cpp`, `DummyObject.cpp`

## Qué reportó el dueño (con captura)
Al inicio de Drive-Thru, en la cinemática de la puerta de la casa ("El respeto hay que ganárselo, como el dinero"), la puerta del mapa sigue ahí, encima de la del decorado de la escena. La ven **los dos jugadores**. Pidió arreglar "este tipo de bugs" en general.

## Qué hacer
1. **Diagnóstico que se pueda leer después de jugar.** Al arrancar una cinemática y a los 2 s, escribí en `CoopAndreas_cutscene.log`:
   - La lista de ocultos de la escena: modelo, posición y si se encontró la entidad.
   - Por cada modelo oculto, toda entidad de ese modelo en un radio de 30 m: puntero, tipo (building, dummy u object), objectType, si está visible y de dónde salió, si se sabe.

   Con eso la próxima corrida confirma o refuta la hipótesis de T21.
2. **Arreglo robusto aunque todavía no sepamos la causa.** Mientras corre la cinemática, por cada entrada de ocultos, ocultá también cualquier entidad visible de ese modelo cerca de esa posición (radio chico, por ejemplo 2 m) y restaurala al terminar.
   - Ocultala por el mismo camino que usa el juego; citá gta-reversed.
   - No toques las del decorado de la escena (`CCutsceneObject`).
   - Interruptor nuevo (default true) en `INTERRUPTORES.md`.
   - Log `cutscene-hide-extra` cuando ocultes algo que el juego no ocultó.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (inicio de Drive-Thru) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
