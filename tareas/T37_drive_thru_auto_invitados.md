# Tarea T37_drive_thru_auto_invitados: auto para los invitados despues del Cluckin' Bell
Lee `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-09_claude.md` y el resultado de T35 en esa tanda. Sin cambios de protocolo salvo que sea imprescindible (si lo es, frena y explica). No toques `main.scm`.

## Pedido del dueño
En Drive-Thru (mision 15, script `SWEET3`), despues de la escena de comprar la comida el host queda al volante y Ryder, Smoke y Sweet ocupan los otros tres asientos: los invitados quedan sin lugar. No cambies eso. Dales un auto para seguir la persecucion.

## Datos
- Tras la escena el script hace `Car.SetCoordinates($sweet_car, 2396.36, -1917.96, 12.38)`, `Car.SetHeading($sweet_car, 267.3)` y, si el host no esta adentro, `Char.WarpIntoCar($scplayer, $sweet_car)` (`_refs\project_gta_sa_coop\CoopAndreas\scm\scripts\SWEET3.txt`, cerca de la linea 826). El auto enemigo aparece en 2411.31, -1928.84, 12.39: no estaciones ahi.
- `Char.WarpIntoCar` solo corre si el host no estaba adentro, asi que no sirve de disparador. Usa `SET_CAR_COORDINATES` con esa posicion (tolerancia 3 m), una sola vez por mision.
- `ParkCopiesNextTo` usa `FindParkingSpot`, con separacion de 1.4 m pensada para motos: para un auto se superpone. Usa un lugar libre para auto (mira `FindFreeCarPosition` en `CCommandPutNetworkPlayersInCar.cpp`) y verifica que quede sobre suelo y lejos del auto enemigo.

## Cambio
Detras de un interruptor nuevo `mission-drive-thru-guest-car` (default true, documentado en `INTERRUPTORES.md`):
1. Solo en la autoridad, en `CMissionSyncRules::OnCommandExecuted`, al ver ese `SET_CAR_COORDINATES` de `SWEET3`: un auto del mismo modelo y colores que `$sweet_car` para los invitados incluidos. Con 1 a 3 invitados alcanza un solo auto de 4 plazas, siempre que todos lo vean; usa el mismo camino que T35 resolvio para que el vehiculo sea visible para todos. Si solo se puede con copias locales, una copia por invitado, y explicalo en el resumen.
2. Con blip para los invitados y liberado al terminar la mision, como las demas copias.
3. Log `rule-drive-thru-guest-car guests=%d cars=%d`.

No cambies el asiento del host, los NPC, ni la regla de `CanUseGuestCar`. Si la escena se saltea o se repite, no crees un segundo auto.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
