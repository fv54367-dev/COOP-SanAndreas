# Tarea T36_aliado_vuelve_del_interior: el aliado que entro con el host tambien sale con el
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Problema (probado en juego)
En Cleaning the Hood Ryder entra con el host (`rule-ally-follow-area warp ped=7174 from=0 to=5`) pero al salir no lo sigue. El log no muestra ninguna linea de esa salida: `ProcessAllyFollowArea` (`client/src/CMissionSyncRules.cpp:284`) descarta en silencio si el filtro `bAcceptNpcGroup`/`m_nArea` de `ms_spawnPoint` no pasa.

## Cambio
En `ProcessAllyFollowArea`, sin interruptor nuevo (va dentro de `mission-ally-follow-area`):
1. Recorda el area interior y la mision de la ultima entrada en la que se movio al menos un aliado. Si el host vuelve de esa misma area al exterior (area 0) en la misma mision, `pending = true` aunque el filtro de `ms_spawnPoint` no pase. Una transicion nueva o un cambio de mision borra ese recuerdo.
2. Se sigue moviendo solo a quienes sigan siendo miembros del grupo del host: si el script saco a Ryder del grupo para una escena (`Char.RemoveFromGroup`, `SWEET1B.txt:1678`), no se lo toca.
3. Log de cada cambio de area durante la mision: `rule-ally-follow-area change from=%d to=%d pending=%d spawn=%d group=%d`, para diagnosticar con el proximo log.

No cambies la logica de `CEntryExitMarkerSync` ni `ProcessEntryWindow`.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
