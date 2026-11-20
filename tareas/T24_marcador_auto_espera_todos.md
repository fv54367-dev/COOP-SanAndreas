# Tarea T24_marcador_auto_espera_todos: un LOCATE_CAR con un invitado al volante espera a todos
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Corrige un hueco de T23 en `FilterCompareFlag` (`client/src/CMissionSyncRules.cpp`). Sin cambios de protocolo.

## Problema (probado en juego)
Drive-Thru (SWEET3, marcador `not Car.Locate3D($sweet_car, $blob_flag, ...)`) y otras misiones: el invitado llega con el auto de misión y la escena arranca sin el host.
El script pregunta directamente por el AUTO (opcodes 0x01AD..0x01B0, 0x00B0, 0x00B1, 0x01AB, 0x01AC), así que el resultado ya es verdadero por sí solo:
- `bGuestCarOverride` queda falso y no se ejecuta la espera de T23.
- En el bloque `locate.bCarSelf`, si el script nunca preguntó `IS_CHAR_IN_CAR`, `pGuestCarScript` no está seteado y se devuelve `state` sin esperar a nadie.
- `AreMissionPlayersNear` solo recorre jugadores de red: nunca mira al host.

## Cambio
Definí `bGuestDrivenCar`: `locate.bCarSelf`, interruptor `ms_bMissionAnyPlayerInCar` encendido y `FindGuestInMissionCar(CPools::GetVehicle(ScriptParams[0]))` no nulo. No lo condiciones a `CanUseGuestCar` (esto vale para todas las misiones, también las de `requiresHostCar`).
1. En el bloque `locate.bCarSelf`, `bGuestDrivenCar` también deja seguir.
2. Si `bGuestDrivenCar` y `locate.radius <= 20.0f`: aplicá la espera de T23 al host (misma tolerancia con `IsPedNearMissionLocate`, mismas excepciones de host muerto o arrestado, mismo mensaje y log `rule-wait-host-guest-car`) y además exigí `AreMissionPlayersNear` aunque `locate.bSphere` sea falso (mensaje y log `rule-wait` como el existente). El vehículo para el área es el de `ScriptParams[0]`.
3. El resultado sigue siendo el valor crudo: con la forma `not` el `false` mantiene el bucle del script.

Reutilizá el código de T23 en lugar de duplicarlo.

Verificá además que los marcadores de INICIO de misión no tengan el mismo hueco (`FilterMissionStart`: un invitado en modo coop nunca arranca). Si encontrás uno, describilo en el resumen sin cambiarlo.

## No cambia
LOCATE grandes (`radius > 20`), autos sin invitado adentro, autos que no son de misión, host como pasajero del mismo auto (ya está cerca).

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤6 líneas.
