# Tarea T83: NPCs fijos de interiores duplicados y sin animación para el que entra después

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md`.

Código:
- `client/src/CInteriorOwnership.*`
- `CNetworkPedManager.*`
- `CSpawnWatch.cpp` (`dup-ped`)
- población de interiores en gta-reversed: `Interior*.cpp`, `CInteriorManager`, `PopulateInterior`, peds de `CInteriorGroup`

## Qué reportó el dueño
1. Si el invitado entra primero a un interior y después entra el host, los NPCs fijos se duplican (el barbero en la misión de Ryder, los pizzeros de la pizzería). Verificá también el orden inverso: host primero, invitado después.
2. Que se sincronicen las animaciones de esos NPCs: el primero que carga el interior los ve sentados o trabajando; los que entran después los ven parados y sin animación.

## Qué hacer
1. Encontrá quién crea los NPCs de interior en cada juego y quién los replica.
2. Elegí un solo dueño por interior: el primero que entra, con traspaso si sale. Los demás no generan los suyos y ven las copias.
3. Las copias reproducen la tarea o animación del original. Antes de inventar, usá lo que ya existe para animaciones (`CNetworkAnimQueue`, tareas de ped sincronizadas).
4. Poné todo detrás de un interruptor nuevo (default true) en `INTERRUPTORES.md`.
5. Dejá logs de quién genera y quién borra.

## Casos borde
- Interior de misión con peds de script: no tocar los de la misión.
- Los dos entran a la vez.
- El dueño sale y queda el otro.
- Tiendas donde el NPC atiende (barbero, ropa, comida): la tienda tiene que seguir funcionando en los dos.
- 3–4 jugadores.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (pizzería y barbería, en los dos órdenes) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
