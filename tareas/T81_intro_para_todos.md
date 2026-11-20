# Tarea T81: la misión inicial (intro) la juega cada uno, aunque otro la esté corriendo

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md`. Código: `client/src/CMissionLock.*`, `client/src/CMissionMirror.cpp` (`mission-blocked`), lo que maneje la partida nueva y el script `intro` (`_refs\project_gta_sa_coop\CoopAndreas\scm\scripts`, sin tocar `main.scm`).

## Qué pidió el dueño
Al hacer partida nueva, si otro jugador ya está corriendo la misión inicial (cinemática del aeropuerto, script `intro`, id 2) sale "X está haciendo esta misión, no podés hacerla" y aparece directamente en Grove. Todos tienen que poder jugarla, sin importar que otro ya la esté corriendo. En los logs: `event=mission-blocked id=2`.

## Qué hacer
Detrás de un interruptor nuevo (default true, en `INTERRUPTORES.md`), la intro no pasa por el bloqueo de misión. Cada jugador la corre en su juego, como individual:
- No se espeja.
- No toma autoridad de misión.
- No le cambia la misión a los demás.

Al terminar, todos quedan como siempre (progreso del host, Grove).

Revisá además dos situaciones:
- **El host arranca Big Smoke mientras otro está en la intro:** el que está en la intro la termina y después se suma a lo normal. El marcador lo espera, como a cualquier jugador que no está.
- **Lo que ven los demás:** la copia del jugador que está en la intro no tiene que aparecer en la cinemática del otro. Ya existe `CCutscenePartners`.

## Casos borde
- Los dos hacen partida nueva al mismo tiempo.
- Uno se desconecta durante la intro.
- El host está en la intro y el invitado no.
- 3–4 jugadores.
- Interruptor apagado: comportamiento de hoy.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
