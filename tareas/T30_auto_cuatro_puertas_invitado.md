# Tarea T30_auto_cuatro_puertas_invitado: Sweet's Girl avanza con el invitado al volante del auto de 4 puertas
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Si necesitás protocolo, la versión ya es 0.4.19-alpha (T25): no la subas de nuevo. Interruptor existente `mission-any-player-in-car`.

## Pedido del dueño
En el marcador donde hace falta un auto de 4 puertas, tiene que alcanzar con que un invitado lo maneje, siempre que los demás jugadores (host incluido) estén CERCA, sin necesidad de estar adentro.

## Evidencia (`HOODS5.txt`)
- `:846` marcador `LocateAnyMeans3D($scplayer, 1, 2764.272, -1979.494, 12.5394, 4, 4, 4)`.
- `:849` `IsInAnyCar($scplayer)`, `:851` `59@ = StoreCarIsIn($scplayer)` (0x00D9), `:855-884` comprueba plazas, clase y modelo.
- `:886-888` `SetControl False`, `Task.CarTempAction($scplayer, 59@, ...)`, fundido.
- `:1000` mueve 59@; `:1052/:1060` Sweet y Kendl suben como pasajeras en los asientos 0 y 1; **`:1152` `59@ = Char.GetCarIsUsing($scplayer)`** y `:1205-1218` siguen usando ese auto. O sea: después del fundido el script da por hecho que el host está adentro.
- HOODS5 está en `requiresHostCar` por eso: no lo saques de la lista de forma general.

## Cambio
Regla solo para este marcador. Cuando un invitado incluido maneja un auto que pase las mismas condiciones del script (clase 0..3 o modelo 596/420, y 2 o más pasajeros máximos), está dentro del marcador, y el host y los demás jugadores están cerca (`IsPedNearMissionLocate` / `AreMissionPlayersNear`): hacé que el script vea ese auto y que el host termine adentro antes de `:1152`.

Elegí la forma más simple y segura después de leer `CCommandPutNetworkPlayersInCar`, `rule-boarding` y `CanUseGuestCar`. La opción de referencia: durante el fundido, dejar al host al volante y al invitado en un asiento que no sea 0 ni 1 (si es el asiento 2 y está libre), para que el script siga por su camino nativo; revisá quién queda como dueño de red del auto.

Si no se puede hacer sin romper la misión (por ejemplo, con 3–4 jugadores no entran todos en el auto), NO lo implementes: explicá en la tanda qué lo impide y qué alternativa propones. Eso vale como resultado.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤8 líneas.
