# Tarea T34_balas_sin_empuje: las balas de otros jugadores no empujan autos
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo.

## Problema (probado en juego)
Tras T26 (normal valida) los autos siguen saliendo volando al disparar desde un auto, y un jugador a pie que le dispara a un auto lo mueve. La normal no era la unica causa: `CWeapon::DoBulletImpact` aplica `ApplyForce` sobre el vehiculo victima (`_refs\gta-reversed\source\game_sa\Weapon.cpp:609-622`) y la repeticion remota en `client/src/PacketHandlers/players.cpp` (handler de `PlayerBulletShot`) lo ejecuta sobre autos que maneja otro jugador.

## Cambio
Detras de un interruptor nuevo `remote-bullet-no-force` (default true, documentado en `INTERRUPTORES.md`):
1. En el handler remoto, si `victim` es un vehiculo valido: guardar `m_vecMoveSpeed` y `m_vecTurnSpeed` antes de `DoBulletImpact` y restaurarlos despues. El dano, las gomas y los efectos siguen igual.
2. En `CWeapon__DoBulletImpact_Hook` (`client/src/Hooks/PlayerHooks.cpp`), rama del jugador local: lo mismo, solo si `victim` es el vehiculo en el que va sentado el propio tirador.
3. Log `bullet-force-cancelled weapon=%d model=%d remote=%d dv=%.3f` cuando el cambio de velocidad cancelado sea mayor a 0.05, con limite de una linea cada 2 s, para confirmar en el log si este era el camino.

No cambies cuanto dano hace ninguna arma ni las balas sobre peds. No toques NPC que disparan.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
