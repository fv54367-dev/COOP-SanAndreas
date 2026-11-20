# Tarea T70: puestos y máquinas, fase 2 (las 5 que quedaron afuera de T67)
Leé `AGENTS.md`, `tareas\T67_puestos_especiales_del_invitado.md` y la sección T67 de `tareas\TANDA_2026-11-10_sacoop_karimov.md` (qué falta por misión). Seguí con `client/src/CMissionMachines.*` y `coop-mission-machines`. No toques `main.scm` ni `scm/`.

## Primero, el sistema compartido
**Canal de "objeto movido por el invitado":** cuando el invitado mueve en su juego una copia de un objeto de misión (imán, carretilla, gancho), el dueño de la misión mueve el objeto **original** igual (posición, rotación, velocidad, enganchado/suelto), con un solo dueño a la vez (quien lo mueve) y sin que `CObjectSync` lo vuelva a pisar mientras tanto. Sin esto no sale ninguna de las cinco. Hacelo reutilizable (`CObjectSync` o archivo propio) y con su caso de desconexión: el objeto queda donde estaba.

## Después, en este orden (la que no salga limpia, no la fuerces: anotala con qué falta)
1. Robbing Uncle Sam: las cajas que el invitado empuja/apila con su carretilla se mueven y cuentan en el dueño.
2. New Model Army (ZERO4): RC Goblin del invitado con imán sobre la mesa (interior área 10: cámara/piso de `RcToys` de sa-coop, `kParked`/`kTableView`/`FloorWait`), barriles movidos con el canal de arriba.
3. Up, Up and Away!: torreta propia del invitado (como ZERO1) y, si entra, el cabrestante del Leviathan con el objeto colgado.
4. Customs Fast Track: controles del invitado ruteados a la grúa (`CScriptedCrane`) del dueño; contenedores compartidos.
5. Cop Wheels: el invitado maneja una moto de misión del dueño y la carga al camión cuenta en el dueño.

## Reglas
Las de T67. Protocolo: si hace falta, la versión siguiente a la de `shared/config.h` (hoy 0.4.33-alpha); paquetes al final. CRLF, compilar client/server/testbot, un commit por misión que salga (GLM, SIN REVISAR, cita MIT, `Co-Authored-By: GLM <noreply@z.ai>`), push a `tanda/2026-11-10-glm`, nunca a main. Pruebas simples en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`.
