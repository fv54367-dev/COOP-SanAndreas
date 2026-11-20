# Tarea T41_nines_subida_tras_emmet: Nines and AKs sube a los invitados al volver de Emmet
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Problema (log real)
Al volver de Emmet: `rule-boarding vehicle=466 walk=0 copies=0 far=1`. `PutPlayersInCar` con `bMissionBoarding` salta a los invitados a mas de `BOARDING_WALK_RADIUS` que tienen asiento libre (`if (distant) { farPlayers++; if (seat >= 0) continue; }`, `CCommandPutNetworkPlayersInCar.cpp`).

## Cambio
Detras de un interruptor nuevo `mission-nines-far-boarding` (default true, documentado en `INTERRUPTORES.md`): solo en la mision 16 (`SWEET2`), un invitado lejos con asiento libre recibe el paquete con su asiento y `walk=false` (se warpea de pasajero). El resto de las misiones y el comportamiento de los que estan cerca no cambian. Se mantienen los filtros de ya en vehiculo, muerto y otra area. Asegurate de no ocupar el asiento de un NPC que esta por entrar.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
