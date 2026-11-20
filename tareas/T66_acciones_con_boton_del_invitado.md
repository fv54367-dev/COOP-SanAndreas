# Tarea T66: acciones "llegar + botón" del invitado (cargas, micrófono, cajas del tren)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y lo que dejaron **T63** y **T64** (tanda y commits). Esto es la **fase 2 de T63** (los "tramos largos" de `mission-rules` de sa-coop). No toques `main.scm` ni `scm/`.

## Problema (auditoría del Codex del amigo, 2026-11-10; verificalo)
El script del dueño comprueba posición + botón + secuencia de **su** CJ; el invitado llega y aprieta, y no pasa nada.
- Dam and Blast (`HEIST2`): colocar las cinco cargas (pista `scm/scripts/HEIST2.txt` ~2176, solo para ubicar la secuencia).
- Amphibious Assault (`WUZI2`): colocar el micrófono en el barco (`WUZI2.txt` ~1159).
- Catalyst: lanzar las cajas del tren a Ryder (potencia y lanzamiento con los botones del dueño). **Si T63 ya lo hizo, saltealo.**

## Referencia (MIT)
`_refs\sa-coop-karimov\src\client\features\story\mission-rules\MissionRules.cpp`: `BeforeCommand`/`SkipCommand`/`AfterCommand` (un tramo del script "va para el otro jugador" entero, con su animación, sin tocar los controles del dueño), `kSpots`, y `Throw`/`HostThrow` para Catalyst.

## Cambio
Interruptor: el mismo `mission-guest-counts` de T63 o uno nuevo `mission-guest-actions` (default **true**, `INTERRUPTORES.md`); apagado: como hoy.
- El tramo "CJ en el lugar + botón → animación de colocar → objeto creado → contador" corre para el invitado que cumple la condición: la animación la hace la copia del invitado (y su propio CJ en su juego), el objeto se crea en el dueño y se ve en todos, el contador avanza una vez.
- El CJ del dueño nunca se mueve, no se congela ni pierde el control por una acción del invitado.
- Listá en la tanda cada tramo portado (misión, opcode, lugar en el `main.scm` original).

## Casos borde
- Dos jugadores en la misma carga a la vez: cuenta una.
- Invitado que muere a mitad de la animación: regla de muerte (falla para todos); que no quede ninguna animación colgada.
- Interior/área distinta: no cuenta.

## Aceptación
Compilan client/server/testbot. CRLF. Cita MIT. 1 prueba por misión en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda. No instales. Resumen ≤10 líneas.
