# Tarea T54: Life's a Beach en coop (baile en equipo)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` (incluida la sección de T53, que usa la misma base) y `docs/MISIONES_MODO.md`. No toques `main.scm` ni `scm/`.

## Pedido del dueño
En Life's a Beach (`MUSIC1`) el baile (script streameado `DANCE`, dance.scm, índice 35) se juega en equipo: cada invitado coop baila en su propio juego al lado del dueño, con sus flechas, sus pasos y su feedback, y **su puntaje se suma al del dueño** (`$21184`, hacen falta 2500). La misión se aprueba o falla para todos.

## Referencia (MIT)
`_refs\sa-coop-karimov\src\client\features\story\minigames\Minigames.cpp`, parte `KIND_DANCE`: el juego del compañero corre el mismo `DANCE` con las cinco cámaras fijas que pone `MUSIC1` (`kDanceCams` 21260, 30 floats), `kDanceFlags` 21180, `kDanceScore` 21184, `kDanceQuit` 21192; un abandono del compañero solo resta lo suyo; el host retiene el final para los últimos puntos. Reusá lo que T53 haya dejado (módulo de minijuegos y paquetes): no dupliques.

## Cambio
Interruptor `coop-dance` (cliente, default **true**, en `INTERRUPTORES.md`). Apagado: como hoy.
- Invitado coop en `MUSIC1` al llegar al baile: arranca su `DANCE` local con las cámaras del dueño, juzgado contra su propia reproducción; manda los puntos al dueño, que los suma.
- Su CJ baila en los otros juegos al menos con las animaciones sincronizadas que ya hay (si no se ve, decilo).
- Actualizá `docs/MISIONES_MODO.md`.

## Casos borde
- 3–4 jugadores: todos bailan y suman; posiciones distintas para que no se pisen.
- Invitado que se va / abandona el baile: deja de sumar, no falla la misión.
- Interruptor apagado o invitado en individual: como hoy.
- Otras misiones que usan `DANCE` (si hay): no se tocan salvo que sea el mismo caso; decilo.

## Aceptación
Igual que T53 (compila client/server/testbot, CRLF, cita MIT, `TESTEAR_HOY.md`, `INTERRUPTORES.md`, tanda). No instales. Resumen ≤10 líneas.
