# Tarea T29_enemigos_cualquier_jugador: Sweet's Girl activa a los enemigos si cualquier jugador de la misión se acerca
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. Interruptor nuevo `mission-any-player-trigger` (por defecto `true`; documentalo en `INTERRUPTORES.md`).

## Problema (pedido del dueño)
Sweet's Girl (`HOODS5`): los enemigos dependen de que el HOST se acerque: `HOODS5.txt:604`, `Char.LocateAnyMeans3D($scplayer, 0, 2775.793, -1942.909, 15.6576, 100.0, 100.0, 50.0)`. Si llega primero un invitado, no aparece nadie.

## Cambio
En `FilterCompareFlag` (`client/src/CMissionSyncRules.cpp`), con una tabla chica `{script, x, y, z, radio}` para poder sumar otras misiones después (hoy una sola fila): cuando el resultado crudo es falso, el opcode es 0x00FE sobre el jugador local, esta PC es la autoridad, el script es de misión sincronizado y las coordenadas y el radio coinciden con la fila (comparación con tolerancia chica), devolvé verdadero si algún jugador de red incluido con `ShouldIncludeNetworkPlayer` está vivo, en la misma área y dentro de la caja del LOCATE (misma semántica por eje que el nativo). Log `rule-any-player-trigger` cada 5 s como máximo.

Hacelo antes de `FilterGuestCar` y sin que entre en las esperas de marcador. Las demás condiciones de HOODS5 no se tocan.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤6 líneas.
