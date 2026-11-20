# Tarea T91: Home Invasion, ajustes de la prueba del 2026-11-10 (sigue a T64/T69/T71/T74)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, las tandas de T64, T69, T71 y T74 (`tareas\TANDA_2026-11-10_codex_t6*.md`, `..._t71.md`, `..._t74.md`). Código: `client/src/CHomeInvasion.*`, `CEntryExitTransitionSync.*`, `CInteriorOwnership.*`, `CCameraMirror.*`, `CObjectSync.cpp`. Script original `_refs\project_gta_sa_coop\CoopAndreas\scm\scripts` (BURGLARY / Home Invasion), sin tocar `main.scm`.

## Qué reportó el dueño (prueba 2026-11-10 noche)
1. **Entrar por cuenta propia:** si el host entra primero a la casa a robar, a los demás les toma la cámara y los mete automáticamente al interior. No tienen que ser metidos: cada uno entra por la puerta cuando quiere. Encontrá quién los mete (cámara espejada, transición de entrada sincronizada, regla de "aliado sigue al dueño") y desactivalo solo para esta misión.
2. **Cajas en paralelo:** aunque otro jugador esté agarrando una caja o ya tenga una, los demás tienen que poder agarrar otras cajas. Hoy queda bloqueado. Una caja la lleva uno solo, pero cada jugador puede llevar la suya al mismo tiempo.
3. **Barra de silencio/ruido:** sigue desapareciendo cuando el host sale del interior (T74 debía dejar una sola barra para todos). Tiene que seguir visible para los que están adentro mientras la misión siga, aunque el host esté afuera.
4. **Entregar en el camión sin esperar:** que no haga falta que los jugadores estén afuera para entregar una caja en el camión. Cada uno entrega la suya cuando llega, aunque el resto siga adentro.

## Casos borde
- 3–4 jugadores.
- El host adentro y los invitados afuera, y al revés.
- Dos jugadores van a la misma caja a la vez: la toma el primero.
- Un jugador muere o se desconecta con una caja en la mano: la caja queda donde estaba y otro puede tomarla.
- Se despierta el dueño de casa: la misión falla para todos.
- `coop-home-invasion=false`: igual que antes.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (sección Home Invasion) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
