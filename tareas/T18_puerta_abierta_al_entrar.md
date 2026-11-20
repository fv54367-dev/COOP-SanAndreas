# Tarea T18_puerta_abierta_al_entrar: la puerta no se cierra para el que todavía no entró
Leé `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-08d.md` y las notas de T15 y T7 en `tareas\TANDA_2026-11-08c.md`.

## Problema
La espera al entrar (`mission-wait-on-entry`, T15) no sirve si la misión cierra la puerta antes de ese LOCATE: T15 listó CAT4, DESERT9, HEIST2 y CASINO6. El invitado queda afuera hasta que a los 30 s lo traen. Si el host ya está adentro, la puerta no debería cerrarse para quien sigue afuera.

## Cambio
En `CEntryExitMarkerSync::Receive` (`client/src/CEntryExitMarkerSync.cpp`), con `mission-interior-safety`: si el jugador local participa de la misión coop de la autoridad, está afuera (área 0), y llega un cierre (`bEnableAccess` de true a false) de la entrada cuyo interior es el área de la copia de la autoridad, mantenelo abierto y diferí el cierre con el mecanismo que ya existe (`ms_bDeferredAccess`/`ms_lastData`). Aplicá el estado real al entrar (ya no está afuera), o a los 35 s como tope, o si la autoridad sale de ese interior. Cuidá que el `Process` actual, que aplica al quedar en área 0, no lo revierta enseguida. Un cierre de otra puerta o con la autoridad afuera se aplica como hoy. Log `enex-keep-open area=` limitado.

## Aceptación
Compila el client. Diff acotado + `TESTEAR_HOY.md`. Resumen ≤8 líneas.
