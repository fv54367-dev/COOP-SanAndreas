# Tarea T74: Home Invasion, UNA sola barra de ruido para todos
Leé `AGENTS.md`, `tareas\TANDA_2026-11-10_codex_t69.md` y `tareas\TANDA_2026-11-10_codex_t71.md`, y `client/src/CHomeInvasion.*`. No toques `main.scm` ni `scm/`.

## Pedido del dueño (2026-11-10)
"Sobre lo de la barra de silencio y eso, la idea es que sea **compartido**, no que cada uno tenga su propio ruido y barra."
Regla general del proyecto desde hoy: barras y contadores de una misión coop son **uno solo para todos**.

## Cambio
- La barra de ruido es **la del dueño de la misión** (la que maneja GUNS1 en su juego: BAD5A/BAD66). El ruido que hace cada invitado (correr, chocar, disparar, lo que el juego cuente como ruido para su CJ) se **suma a esa misma barra** en el juego del dueño; si se llena, el coronel se despierta como siempre, lo haya llenado quien sea.
- Los invitados **ven la misma barra** que el dueño (valor espejado, el mismo HUD del juego), no una propia.
- Sacá la barra propia por invitado de T69 (o dejá de mostrarla y de usarla). El resto de T69/T71 (balaclava, sigilo, cajas) no se toca.
- Ruido con el dueño afuera de la casa: si el script del dueño no está midiendo ruido (94@=0, ver tanda T69), no se suma; los invitados ven lo mismo que el dueño.

## Casos borde
3–4 jugadores suman todos a la misma barra; invitado en individual no suma; desconexión: deja de sumar; fin/reintento: la barra la resetea el script como siempre.

## Aceptación
Compilan client/server/testbot; `tools\correr_testbot.bat` TODO PASS (versión siguiente si hay protocolo). CRLF. Actualizá la prueba de ruido de GUNS1 en `TESTEAR_HOY.md` ("el invitado hace ruido y sube la barra que ven todos; si se llena se despierta el coronel"), `INTERRUPTORES.md` si cambia algo, tanda T69. No instales. Resumen ≤10 líneas.
