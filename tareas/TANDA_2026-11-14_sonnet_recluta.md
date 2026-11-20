# TANDA 2026-11-14 sonnet_recluta (rama tanda/2026-11-14-recluta, SIN PROBAR)

## 1. Reclutar Grove: todos los jugadores
- Sistema: ya era multijugador (`SyncLocalRecruits`, `PED_RECRUIT`, server con límite de 7 por jugador, no roba reclutas, traspaso con `AssignPed`). El hueco estaba en el juego del invitado: la acción de reclutar de GTA (`CPlayerPed::ProcessGroupBehaviour`, 0x60D350) no corre si el bit `m_bGroupStuffDisabled` del jugador está encendido, y lo deja el script principal (`SET_PLAYER_GROUP_RECRUITMENT`). Desensamblé 0x60C840 (reclutar): solo pide tipo GANG2 / cheat de reclutar, no mira `m_nCreatedBy`, así que las copias MISSION_CHAR no son el problema.
- Causa NO confirmada en juego (hipótesis más probable). Hecho: `EnableLocalRecruiting()` en `CNetworkPedManager::Process` apaga el bit cada frame cuando no hay misión (`OnAMissionFlag`), ni cinemática, y el jugador está vivo. Log `recruit-all-enabled` (máx. 1 cada 10 s) para ver si de verdad estaba encendido.
- Interruptor cliente `recruit-all-players=true`. Sin cambio de protocolo.

## 2. Paramédico: heridos suben a la ambulancia del invitado
- Causa: el AMBULAN original solo mira al dueño (`$scplayer`): `LOCATE_CHAR_IN_CAR_CHAR_3D` (0106) junto al herido, `LOCATE_CHAR_IN_CAR_CHAR_2D` (00F4) para mantener el estado 2 y `LOCATE_STOPPED_CHAR_IN_CAR_3D` (0103) en el hospital. Con el dueño lejos nunca pasan (y el estado 2 vuelve a 1). `34@` es la ambulancia del dueño (el script lo refresca cada frame porque el dueño debe seguir dentro de una ambulancia).
- Hecho: `CPatientHandover::FilterCompare` (enganchado en `FilterCompareFlag`, `CMissionSyncRules.cpp`), solo AMBULAN en la autoridad: fase 1 (80@==1) y el dueño falla el 0106 contra el herido (77@): si un invitado de la misión conduce una ambulancia (416) dentro de la caja, vale true y se recuerda ese invitado por herido; fase 2: 00F4 vale con el invitado dentro de la caja 2D; fase 3: 0103 vale con el invitado parado en el hospital con el herido en su ambulancia. `OnEnterCar` usa la ambulancia del invitado en vez de la del dueño y el traspaso existente (`PED_PATIENT_TASK` ENTER) hace subir al herido con la animación normal en el juego del invitado; GOTO/EXPLODE/RELEASE ya se reenviaban.
- Interruptor cliente `ambulance-guest-patients=true` (necesita `patient-handover`). Log `rule-ambulance-guest phase=1|3`. Sin cambio de protocolo.
- Límites/riesgos: no vi el script original; asumo que el original usa los mismos opcodes y los lvars 77@/80@ (de `scm/scripts/AMBULAN.txt`, versión archivada). Si el original usa otro opcode o radio, el log no sale y hay que ajustar. El contador de pasajeros llenos (`A_FULL`) y el +salud al entregar siguen mirando la ambulancia del dueño.

## Pruebas simples
- [ ] Grove 1: un invitado (no host) apunta a un Grove y recluta: lo sigue; el host lo ve seguir al invitado.
- [ ] Grove 2: host y invitado reclutan cada uno el suyo: no se pisan; H del invitado lo despide.
- [ ] Paramédico 1: dueño lejos del herido, el invitado llega con su ambulancia: el herido sube (animación) a la del invitado.
- [ ] Paramédico 2: el invitado lo lleva al hospital: cuenta la entrega y la misión sigue.
- [ ] Buscar en el log `recruit-all-enabled` y `rule-ambulance-guest`.

## No pude
- Probar nada en juego (instrucción: no abrir el juego). Testbot no tocado (sin cambio de protocolo).
