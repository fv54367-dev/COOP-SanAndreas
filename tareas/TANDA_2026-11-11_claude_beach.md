# T97 (Claude, rama `beach`): Life's a Beach coop y autos de invitados por tramo
Archivos: client/src/CCoopLowrider.cpp, CMissionSyncRules.cpp, COpCodeSync.{h,cpp}, CMissionMirror.cpp, CConfigLoader.{h,cpp}, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md. Sin SCM, sin protocolo, sin server/shared.

## Evidencia (build/logs_2026-11-11)
- J1 (host): MUSIC1 dos intentos; `dance-start` en los dos, `rule-wait 00FF` (marcador de la chica) esperando a Lyrax y `rule-teleport` justo despues de la llegada a la playa (73437406) sin ninguna espera antes.
- J2 no es el invitado: paso a individual en t=72816046 (todos sus `solo-launch` dicen `individual=1`) y el host esperaba a "Lyrax" (el amigo). Del invitado real no hay log: los motivos del invitado quedan deducidos del codigo.

## Causas
1. Llegada: `Char.LocateAnyMeans2D($scplayer, 34@, 35@, 50, 50)` (MUSIC1_3053) no es marcador (sin esfera, radio > 20): ninguna regla de espera lo toma. La escena MUSIC1_14856 hace `Char.SetCoordinates($scplayer, x, y, -100)` y la regla de teletransporte junta a todos.
2. Musica: es la zona de audio 'BEACH' (`SWITCH_AUDIO_ZONE` 0917; gta-reversed AudioZones.cpp SwitchAudioZone 0x508320, AEAmbienceTrackManager usa la zona activa). 0917 no estaba en la tabla de opcodes replicados. Handler del exe 0x476810: ReadTextLabelFromScript(8) + CollectParameters(1), lo cubren los ganchos de texto existentes.
3. Baile: `Dance::Packet`/`Process` del invitado exigian `CCoopMission::IsLocalParticipant()`, que sin CCoopMission activo (solo existe en secundarias via 0x1D29, no en historia ni en scripts originales) da siempre false: `dance-skip reason=not-participant` siempre. Karimov no tiene ese chequeo; el lowrider (GuestEligible) ya usaba `!IsActive() || IsLocalParticipant()`.
4. Auto del invitado: MUSIC1/SMOKE3 en `requiresHostCar` excluian toda la mision. Labels de scm/scripts/*.txt = offsets del bloque original (verificado: MUSIC1_14856 empieza con 01B4, SMOKE3_10370 y _16921 con 00D6 en el main.scm original).

## Arreglo (cada uno con interruptor, ver INTERRUPTORES.md T97)
- `dance-guest-story`, `music1-beach-arrival`, `mission-audio-zones`, `mission-guest-car-sections`.

## Casos borde
- 3-4 jugadores: la llegada espera a todos los incluidos; el baile ya repartia puestos por slot. F1 individual: no incluido en espera, no recibe zonas ni baile.
- Desconexion/caida del host: el invitado apaga las zonas que le prendio la red (OnHostMissionEnded/OnReceiverReset). Invitado que entra a mitad: no recibe la zona (sin snapshot).
- Invitado en interior u otra punta: la llegada espera (como cualquier marcador); morir falla la mision para todos (regla fija).
- Bloque de main.scm distinto (otro main.scm o scripts coop): hash no coincide, exclusion de mision entera como antes.
- Interruptores en false: comportamiento anterior.

## Pendiente
- Revision cruzada (Codex solo lectura) y prueba en juego con 2-4; log del invitado real.
