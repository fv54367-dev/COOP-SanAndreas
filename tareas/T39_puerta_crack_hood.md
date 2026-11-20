# Tarea T39_puerta_crack_hood: en Cleaning the Hood la puerta no queda cerrada si el host salio
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Problema (probado en juego)
El host entra a LACRAK, sale mientras espera a los invitados y el script cierra la puerta (`World.SwitchEntryExit('LACRAK', False)`, `SWEET1B.txt:2382` en `_refs\project_gta_sa_coop\CoopAndreas\scm\scripts`). Ya nadie puede volver a entrar. En el juego original esa salida no existe: la abre la espera de jugadores de la coop.

## Pedido del dueño
Si Ryder sigue adentro da igual, pero el host y los invitados tienen que poder volver a entrar.

## Cambio
Detras de un interruptor nuevo `mission-hood-door-open` (default true, documentado en `INTERRUPTORES.md`), solo en la autoridad, mision `SWEET1B`:
1. Recorda si el host ya estuvo en el area 5 durante esta mision (se borra al cambiar de mision).
2. Despues de ejecutar `SWITCH_ENTRY_EXIT` (0x07FB) si esa mision lo pidio cerrado, el host ya estuvo adentro y **ahora esta en area 0**: volve a habilitar el acceso de las entradas de nombre `LACRAK` y manda `CEntryExitMarkerSync::Send()`. Verifica en gta-reversed como aplica el comando el flag a cada extremo, y que el `Send` propio quede despues del que hace `COpCodeSync.cpp:1657`. Log `rule-hood-door-open`.
3. Los cierres con el host adentro (`SWEET1B.txt:1583`) y el del inicio de la mision (`:274`, el host nunca entro todavia) no cambian.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
