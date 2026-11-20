# Tarea T45_ver_cesar_de_otro: los demas pueden ver a quien juega Cesar Vialpando
Lee `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-09_claude.md` y `docs/MISIONES_MODO.md`. No toques `main.scm`. Sin cambios de protocolo salvo que sea imprescindible (si lo es, frena y explica antes).

## Pedido del dueño
Cuando un jugador hace una mision individual, los demas pueden ver como la hace, incluidos los NPC y autos que aparecen.

## Contexto
`COpCodeSync::IsLocalOnlyMissionRunning` (`COpCodeSync.cpp:661`) solo considera `SWEET6` y `LOWR`. Con eso, `Hooks/WorldHooks.cpp:122` no crea como peds de red a los actores de esa mision, y `CNetworkVehicleManager.cpp:152` trata distinto a sus autos. Se hizo asi porque cada cliente corria su propia copia; hoy `CMissionLock` deja jugarla de a uno por vez.

## Primero investiga (y escribi las conclusiones en la tanda)
1. Que hace hoy con un jugador que mira: peds y autos de una mision individual cualquiera (F1) ya sincronizados por ser no-local-only, y que pasa con los de `SWEET6`.
2. Quien puede quedarse con el control de esos peds y autos (`PedTakeHost`, traspaso de syncer por distancia, `CNetworkPedManager`, `CNetworkVehicleManager`). El script corre solo en quien juega: si un espectador pasa a controlar un ped o auto de la mision, las tareas del script dejan de verse o la mision se traba.
3. Que pasa si el espectador choca, dispara o se sube a algo de la mision.

## Cambio
Detras de un interruptor nuevo `mission-individual-spectate` (default true, documentado en `INTERRUPTORES.md`), solo para `SWEET6` (`LOWR` no se toca):
- Los peds y autos creados por esa mision se ven en los demas clientes, y el que juega conserva el control mientras dure la mision: nadie mas toma su syncer. Al terminar la mision se liberan con la limpieza normal.
- El espectador no puede romper la mision: si no se puede garantizar que el dano o el robo de un auto no la arruine, que esos actores sean invulnerables/no robables en los clientes espectadores.
- Con el interruptor apagado, comportamiento identico al de hoy.

**Si encontras que no se puede hacer sin arriesgar Cesar, que ya funciona, NO lo implementes:** deja solo las conclusiones en la tanda y explica en el resumen que hace falta. Las animaciones de baile y salto de los lowriders probablemente no se repliquen: no intentes replicarlas en esta tarea; solo decilo en el resumen.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
