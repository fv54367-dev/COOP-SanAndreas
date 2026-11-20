# Tarea T67: puestos y máquinas para el invitado (torretas, RC, grúa, imán, carretilla)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y lo que dejaron **T63**, **T64** y **T66**. No toques `main.scm` ni `scm/`.

## Problema (auditoría del Codex del amigo, 2026-11-10; verificalo misión por misión)
| Misión | Qué no puede hacer el invitado |
|---|---|
| Air Raid | manejar la ametralladora fija (no tiene puesto propio) |
| Supply Lines… | pilotar un avión RC propio |
| New Model Army | usar otro helicóptero RC con imán para mover obstáculos |
| Customs Fast Track | operar la grúa para descargar contenedores |
| Up, Up and Away! | torreta y transporte con el imán del Leviathan (sin sync del cabrestante) |
| Robbing Uncle Sam | que las cajas movidas con su carretilla existan y cuenten |
| Cop Wheels | que la moto que entrega sea reconocida y cargada en el camión |
Pista: `scm/scripts/ZERO4.txt` ~2775 (imán de Zero), solo para ubicar la secuencia.

## Referencia (MIT)
`_refs\sa-coop-karimov`: `features\story\rc-toys\RcToys.cpp` (RC del invitado), lo que tengan de torretas/grúa/imán/cabrestante (grep `turret`, `crane`, `magnet`, `winch`, `forklift`, `RC_`), `features\mission-objects\Objects.cpp`. Su README dice que el compañero vuela/maneja vehículos de misión y que hay torretas y trenes de aterrizaje sincronizados.

## Cambio
Interruptor `coop-mission-machines` (cliente, default **true**, `INTERRUPTORES.md`); apagado: como hoy.
- Un sistema por tipo de máquina (puesto fijo/torreta, RC propio, imán/cabrestante, grúa), no un parche por misión; cada misión usa el sistema que le toca.
- Lo que el invitado hace (daño con la torreta, objeto movido con el imán, contenedor descargado, caja apilada, moto entregada) cuenta en el script del dueño una sola vez (T63/T64).
- Si una misión no se puede hacer limpia, **no la fuerces**: dejala anotada en la tanda con qué falta. Mejor 4 bien que 7 a medias.

## Casos borde
- 3–4 jugadores: si hay un solo puesto, lo toma uno y los demás siguen como hoy.
- El invitado se baja/desconecta del puesto o del RC: vuelve a su CJ sin quedar trabado; el RC se borra.
- Imán con un objeto agarrado cuando el que lo maneja se va: el objeto cae.

## Aceptación
Compilan client/server/testbot (versión si hay protocolo). CRLF. Cita MIT. 1 prueba por misión hecha en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda. No instales. Resumen ≤10 líneas.

## Reasignada (2026-11-10)
**Esta tarea la hace GLM en `E:\juegos\gtasacoopnuevo\wt-glm` (rama `tanda/2026-11-10-glm`).** Si sos el Codex de la cadena en `coopandreas-server`: no hagas nada, no modifiques ningún archivo y terminá respondiendo "T67 reasignada a GLM".
