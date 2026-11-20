# Tarea T75: las 4 misiones de máquinas que faltan (después de T67 y T70)
Leé `AGENTS.md`, `tareas\T70_maquinas_fase2.md` y las secciones T67/T70 de `tareas\TANDA_2026-11-10_sacoop_karimov.md` (qué falta en cada una). Ya existen: `client/src/CMissionMachines.*` (puestos/RC/carretilla, `coop-mission-machines`) y el canal reutilizable `CMissionObjectMoves` (`GUEST_OBJECT_MOVE`: el invitado mueve una copia y el dueño mueve el original). **GLM está arreglando en paralelo hallazgos de `CMissionMachines.cpp`** (funciones `Back`, `TakeAway`, `OnDamage`): no reescribas esas funciones; agregá lo tuyo en funciones/archivos nuevos. No toques `main.scm` ni `scm/`.

Referencia MIT: `_refs\sa-coop-karimov\src\client\features\story\rc-toys\RcToys.cpp` (área 10: `kParked`, `kTableView`, `FloorWait`) y lo que tengan de grúa/imán/cabrestante (grep `crane`, `magnet`, `winch`, `Leviathan`, `COPCAR`/`Cop Wheels`).

En este orden; **la que no salga limpia, no la fuerces**, anotala con qué falta:
1. **New Model Army (ZERO4):** RC Goblin del invitado con imán sobre la mesa (interior área 10: cámara/piso como RcToys), barriles movidos por `CMissionObjectMoves`.
2. **Up, Up and Away!:** torreta propia del invitado (como ZERO1) y, si entra, el imán/cabrestante del Leviathan con el objeto colgado (objeto movido por el canal).
3. **Customs Fast Track:** controles del invitado ruteados a la grúa (`CScriptedCrane`) del dueño; contenedores compartidos.
4. **Cop Wheels:** el invitado maneja una moto de misión del dueño y la carga al camión cuenta en el dueño.

Reglas de siempre: lo que hace el invitado cuenta **una vez** en el script del dueño; el CJ del dueño nunca se mueve ni pierde el control; dinero/progreso de cada uno; barras/contadores de la misión son **uno solo para todos**. Protocolo: versión siguiente a la de `shared/config.h`, paquetes al final.

## Aceptación
Compilan client/server/testbot; `tools\correr_testbot.bat` TODO PASS. CRLF. Cita MIT. Una prueba simple por misión hecha en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda. No instales. Resumen ≤10 líneas.
