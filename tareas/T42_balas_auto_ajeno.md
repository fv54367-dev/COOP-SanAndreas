# Tarea T42_balas_auto_ajeno: tampoco empujar en la pantalla del que dispara
Lee `tareas\COMUN_reglas.md` y T34 en `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo.

## Problema (probado en juego)
Con un jugador quieto manejando y otro disparandole, el auto se mueve. El juego original empuja un poco en cada tiro (`Weapon.cpp:609-622`: fuerza -4 a -20). T34 lo cancela en la repeticion remota y en el propio auto del tirador, pero la rama local de `CWeapon__DoBulletImpact_Hook` (`client/src/Hooks/PlayerHooks.cpp`) sigue empujando la copia local de un auto que maneja otro jugador. El log `bullet-force-cancelled` no salio nunca porque el umbral 0.05 es mas alto que el empuje de una bala.

## Cambio
Dentro de `remote-bullet-no-force`:
1. Rama local: el guard tambien se aplica si el conductor o algun pasajero de `victim` es otro jugador de la red (`CNetworkPlayerManager::GetPlayer`).
2. Baja el umbral del log a 0.002 (sigue limitado a una linea cada 2 s).

No cambies el dano. Autos sin jugadores adentro y autos de NPC se comportan como siempre.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
