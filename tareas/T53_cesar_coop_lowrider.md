# Tarea T53: César Vialpando en coop (concurso de lowrider en equipo)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y `docs/MISIONES_MODO.md` (fila Cesar Vialpando). No toques `main.scm` ni `scm/`. Scripts originales (`vanilla-scripts=true`).

## Pedido del dueño
César Vialpando (`SWEET6`, concurso de lowrider con `LOWGAME`/`BDISPLY`) jugable en coop: el dueño de la misión salta con el auto de CJ como siempre y cada invitado en coop salta con **su propio** auto al lado, con sus flechas, y **sus puntos se suman al puntaje del dueño** contra el mismo rival. La misión se aprueba o falla para todos como cualquier misión coop. Cada uno cobra su dinero (regla: dinero de cada jugador). Sigue existiendo el modo individual (F1).

## Referencia (MIT, se puede portar citando)
`_refs\sa-coop-karimov\src\client\features\story\minigames\Minigames.cpp` (+ `.h`): leé la parte `KIND_LOWRIDER`. Ellos: el auto prestado del compañero (Savanna 567 con hidráulica 1087), la chica propia en la puerta (modelo 64), las cámaras del encuentro, `BDISPLY` arrancado en el juego del compañero, el juicio con las reglas de `LOWGAME`, las flechas sobre la **posición de reproducción del host** (cada juego por su cuenta se desfasaba), los puntos sumados a `$4072`, y el host que retiene el final unos segundos para los últimos puntos. Usá `_refs\gta-reversed` para lo que toque del motor.

## Lo que ya hay acá (investigá antes de diseñar, y reusá)
- `COpCodeSync::IsLocalOnlyMissionRunning` (`client/src/COpCodeSync.cpp` ~661) trata `SWEET6` como local-only; `CMissionLock` y `CMissionSyncRules` lo dejan solo individual (T31, T45). En coop eso tiene que cambiar solo para el modo coop; en individual queda igual.
- `client/src/CMinigameVs.cpp` (VS de baile/lowrider, el dueño descartó el VS): mirá qué reusar (sincronización de beats/puntos) sin reactivar el VS.
- La capa espejo (`CMissionMirror`, `CCameraMirror`, textos) ya pasa textos y cámaras del dueño a los invitados.

## Cambio
Interruptor nuevo `coop-lowrider` (cliente, `coopandreas.ini`, default **true**, documentado en `INTERRUPTORES.md`). Apagado: exactamente lo de hoy (solo individual).
- El marcador de César espera a todos los coop (regla de siempre) y la misión corre en el dueño.
- Cada invitado coop: su auto con hidráulica al lado del de CJ (posición libre, sin chocar), su chica, flechas propias sincronizadas con la reproducción del dueño, juicio local y puntos enviados al dueño, que los suma. Al terminar, se borra todo lo creado para el invitado.
- Se ve el auto de cada uno saltando en los otros juegos (como mínimo la posición/rotación por el sync normal de vehículos; si la hidráulica no se ve, decilo en el resumen).
- Actualizá `docs/REQUISITOS.md` (Cesar Vialpando sale de "Solo individuales": "coop (puntaje en equipo) e individual") y `docs/MISIONES_MODO.md`.

## Casos borde (escribilos en la tanda antes de codear y respetalos)
- 3–4 jugadores: cada invitado con su auto y su chica en lugares distintos; todos suman.
- Invitado que se va o se desconecta en medio: sus objetos se borran; la misión sigue para el resto (no es una "caída").
- Dueño que se va: la misión termina como cualquier misión coop.
- Stream de música trabado en un invitado: sus flechas siguen la posición del dueño (no se congelan).
- Interruptor apagado o invitado en individual: como hoy.

## Aceptación
Compilan client, server y testbot (si hay protocolo, versión subida). CRLF. Diff acotado. Commit con la cita MIT. Una prueba simple en `TESTEAR_HOY.md`, entrada en `INTERRUPTORES.md`, resumen y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤10 líneas.
