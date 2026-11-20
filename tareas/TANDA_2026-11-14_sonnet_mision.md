# TANDA 2026-11-14 sonnet misión (Nines and AK's / Drive-By)

Rama `tanda/2026-11-14-mision`. Todo SIN PROBAR. Sin cambio de protocolo (sin paquetes nuevos, `COOPANDREAS_VERSION` igual).

## 1. Audio de Nines and AK's: diálogos repetidos
- Causa (por lectura, los logs no tienen eventos de audio): el invitado no corre el script de la misión; solo reproduce lo que manda el host (`PLAY_MISSION_AUDIO`: preload y play por slot). El host manda el siguiente preload apenas termina su diálogo, pero el invitado termina el suyo más tarde (carga + latencia): el preload caía sobre un slot que seguía hablando, y la lectura del estado "cargado" en los cuadros siguientes podía ser la del audio anterior, con lo que el diálogo se cortaba o volvía a sonar. SWEET2 recarga el mismo slot enseguida (slots 1 y 2 alternados) así que es la misión donde más se nota; el mecanismo es general.
- Arreglo (`Hooks/MissionAudioHooks.cpp`, general para toda misión con diálogos): preload en espera si el slot sigue hablando (`IsMissionAudioSampleFinished` 0x5072C0, tope 4 s); play del mismo audio a <2,5 s del anterior se ignora; no se lee el estado de carga en los 3 cuadros siguientes a un preload. El bucle de Main.cpp pasó a `MissionAudioHooks::ProcessNetwork()`.
- Interruptor: `mission-audio-guard=true`.

## 2. Nines and AK's: invitados vuelven al auto con animación
- Causa: al terminar la práctica el host se sube al auto de Big Smoke y `ProcessMissionBoarding` llama `PutPlayersInCar(mission boarding)`. Con `mission-no-auto-board` (14-11) esa rama dejaba el asiento libre y no movía a nadie: los invitados quedaban lejos a pie; sin ese interruptor los lejanos se warpeaban adentro.
- Arreglo: solo misión 16, con asiento libre: el paquete `PUT_IN_VEHICLE_SCRIPTED` sale con `walk=1` también para los lejanos y aun con `no-auto-board`. El receptor (`ProcessPendingBoarding`), si está a >6 m del auto, aparece junto a la puerta del asiento (`ComputeTargetDoorToEnterAsPassenger` + `GetPositionToOpenCarDoor`, suelo y colisión validados, de frente al auto) y lanza `TASK_ENTER_CAR_AS_PASSENGER` (05CA). Sin lugar válido no hace nada.
- Interruptor: `mission-nines-board-near=true`.

## 3. Drive-By: 1000 balas de Tec9
- Causa: `ProcessDriveByGuestCars` (y `ProcessWrongSideGuestCars`, Wrong Side of the Tracks) mandaban `MISSION_PLAYER_EQUIPMENT` Tec9 con 1000 fijas. Los scripts SWEET4/SMOKE3 no le dan Tec9 a CJ (solo a los aliados, `$sw4_ammo`=30000 y 99999).
- Arreglo (`CMissionSyncRules.cpp`, `GuestRuleAmmo`): munición que el host tiene de esa arma (tope 1000) o, si no la tiene, 2 cargadores (Tec9: 100). Lo que el script le da al jugador por `OnPlayerEquipment` ya se copiaba tal cual.
- Interruptor: `mission-equipment-script-ammo=true`.

## Interruptores nuevos (cliente `[config]`, true por defecto; false = como antes)
`mission-audio-guard`, `mission-equipment-script-ammo`, `mission-nines-board-near`. Documentados en `INTERRUPTORES.md`.

## Pruebas simples
- [ ] Nines and AK's, host + 1 invitado: los diálogos de Big Smoke/Emmet suenan una vez, sin cortes ni repetidos, en el invitado.
- [ ] Nines and AK's: al terminar la práctica de tiro, el invitado aparece junto a la puerta del auto de Smoke y sube caminando con la animación (no adentro de golpe, no lejos a pie).
- [ ] Nines and AK's con `mission-nines-board-near=false` en el host: vuelve el comportamiento anterior.
- [ ] Drive-By: el invitado recibe Tec9 con ~100 balas (no 1000) al subir el host al 492.
- [ ] Wrong Side of the Tracks: lo mismo en la etapa de la moto.

## Lo que no pude
- No hay eventos de audio en los logs (`CoopAndreas_mirror.log` del usuario y del amigo), así que la causa del audio es por lectura del código; si sigue repitiéndose, mirar los logs `audio-guard` y comparar con el orden de los paquetes.
- No probé nada en el juego (ni lo abrí). No existía `tareas/TANDA_2026-11-14_claude_sabado.md` en la rama.
