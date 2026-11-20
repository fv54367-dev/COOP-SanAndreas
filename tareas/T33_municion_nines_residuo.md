# Tarea T33_municion_nines_residuo: Nines and AKs no deja la pistola con municion casi infinita
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Problema (probado en juego, log del host)
`rule-equipment player=3 weapon=22 ammo=10059 setAmmo=1`. SWEET2 guarda la municion antes de la practica (`41@ = Char.GetAmmoInWeapon($scplayer, 22)`), da 30000, y al final hace `41@ += 60` y `Char.SetAmmo($scplayer, Pistol, 41@)` (`_refs\project_gta_sa_coop\CoopAndreas\scm\scripts\SWEET2.txt:505-506` y `:2893-2897`). El host ya tenia 9999 al empezar (residuo de un intento anterior que fallo con las 30000 puestas), asi que termina con 10059, y T25 se lo copia al invitado.

## Cambio
En `OnPlayerEquipment` (`client/src/CMissionSyncRules.cpp`), solo en la autoridad y detras de un interruptor nuevo `mission-nines-ammo-repair` (default true, documentado en `INTERRUPTORES.md`):
- Si el script es `SWEET2`, el opcode es `0x017B` (SET_CHAR_AMMO), el arma es 22 y la municion es >= 9000: dejar la municion local del host en 60 (`SetAmmo` despues del nativo) y mandar 60 a los invitados en vez del valor del script. Log `rule-ammo-repair from=%d to=60`.
- Los `SET_CHAR_AMMO` de la practica (10 y 30000) no se tocan: 30000 aparece a mitad de mision, asi que el filtro tiene que distinguir el final. Usa que el valor final no es exactamente 30000 ni 10; verifica en el SCM que no haya otro SetAmmo de pistola con valor >= 9000 distinto de 30000.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
