# Tarea T25_municion_igual_al_host: el SET_CHAR_AMMO del host también llega a los invitados
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Esta tarea es la dueña del protocolo de la tanda.

## Problema (probado en juego)
Después de Nines and AKs los invitados se quedan con la pistola casi infinita. SWEET2 hace `Char.GiveWeapon($scplayer, Pistol, 30000)` y al final la deja bien con `Char.SetAmmo($scplayer, Pistol, ...)`. `OnPlayerEquipment` (`client/src/CMissionSyncRules.cpp`) replica 0x01B2, 0x01B9 y 0x0555, pero no `COMMAND_SET_CHAR_AMMO = 0x017B`: el invitado recibe las 30000 balas y nunca el ajuste.

## Cambio
- `MissionPlayerEquipment` (`shared/network/packets/`): campo nuevo `setAmmo` al final.
- `OnCommandExecuted` y `OnPlayerEquipment`: tratar también 0x017B (mismos parámetros que 0x01B2: char, arma, munición). Revisá que el tipo de `packet.ammo` alcance para 30000.
- Handler en `client/src/PacketHandlers/scripts.cpp`: con `setAmmo`, solo si el jugador local tiene esa arma en su slot, poné la munición exacta (`CPed::SetAmmo` u equivalente) y no la entregues de nuevo. Si no la tiene, no hagas nada. Log `rx-equipment` con `setAmmo`.
- El server: si reenvía por campos, actualizalo.
- Subí `COOPANDREAS_VERSION` a `0.4.19-alpha` en `shared/config.h`. No toques `COOPANDREAS_BUILD`.

## No cambia
No replicar `REMOVE_ALL_CHAR_WEAPONS` (0x048F): varias misiones lo usan y devuelven las armas de otra forma. Dinero y estrellas: siempre de cada jugador. Interruptor existente `mission-player-equipment`.

Referencia: gta-reversed `CharacterCommands.cpp` (SetCharAmmo).

## Aceptación
Compilan client, server y testbot (`xmake build -r testbot`). CRLF. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤6 líneas.
