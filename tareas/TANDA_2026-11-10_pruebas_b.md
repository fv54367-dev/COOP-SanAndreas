# Tanda 2026-11-10 (noche): resultados de las pruebas de la tarde y de la Historia temprana (B)

Prueba del dueño con PROBAR_SOLO (2 jugadores, protocolo 0.4.39-alpha). Logs: `GTA_JUGADOR1\CoopAndreas_mirror.log` (host), `GTA_JUGADOR2\CoopAndreas_mirror.log` (invitado), `*_cutscene.log`; copia en `E:\juegos\gtasacoopnuevo\_logs_2026-11-10\` (los de la noche se copian ahí con sufijo `_noche`).

## Funciona (borrar de TESTEAR_HOY)
Robarle la moto a un policía, `/tp <jugador>` en modo prueba, robo entre jugadores (animación del ladrón), GPS del conductor, Big Smoke (intro1) jugable. Partida nueva simultánea durante la cinemática del otro: esta vez no cerró (el logger de abort no tuvo nada que capturar).

## Lo reportado, con tarea
| Tarea | Qué | Dato |
|---|---|---|
| T80 | Trenes siguen duplicados: dos en el mismo riel en sentidos opuestos, otros quietos en la nada | Ni un `train-local-random` en el log del invitado: no son trenes random propios. Hipótesis de Claude abajo |
| T81 | La misión inicial (intro, cinemática del aeropuerto) tiene que poder jugarla cada uno aunque otro la esté corriendo | Hoy: "X está haciendo esta misión" y aparece en Grove (`mission-blocked id=2` en los logs) |
| T82 | El invitado no ve NPCs de misión: Ryder al empezar Cleaning the Hood (el host sí lo ve), Sweet "aire" en la cinemática final de Tagging up Turf, y en general | Hipótesis abajo |
| T83 | NPCs fijos de interiores duplicados (barbero, pizzeros) si el invitado entra primero y después el host; ¿y al revés?; y sus animaciones (sentados, etc.) no se ven en los que entran después | |
| T84 | Volvió el cruce de CJ en cinemáticas (host ve el CJ del invitado y viceversa) después de cambiarse ropa/peinado | Regresión de T48 (`TANDA_2026-11-10_cinematicas.md`) |
| T85 | En las cinemáticas, la cabeza de la copia del otro jugador baja y sube muchas veces (Big Smoke, Ryder: parece general) | Solo la cabeza |
| T86 | Reglas por escena para los invitados en cinemáticas (ver la tarea) | Big Smoke: persigue el auto; cementerio: queda duro; Cleaning the Hood: plano desde el piso y final en Grove; Drive-Thru: Cluckin' Bell |
| T87 | Una puerta del mapa sigue visible en una cinemática (inicio de Drive-Thru, puerta de la casa; los dos jugadores la ven) | Es T21: causa sin demostrar |

## Hipótesis de Claude
**T80 (trenes).** El invitado no tiene trenes random propios (cero `train-local-random`). Lo de más tienen que ser copias que quedaron sueltas: `RemoveCopy` (`CTrainSync.cpp:475`) borra los vagones a través de sus registros y se saltea los que tienen el registro nulo o `m_bSyncing`. Lo que queda en el mundo es una cadena creada con `CreateMissionTrain`, que tiene `bMissionTrain` (así que `RemoveLocalRandomTrains` no la toca) y un conductor NPC. `OverrideCopyProcessControl` solo la frena si `FindByHeadVehicle` la encuentra ligada y con estado. Si no la encuentra, la cadena anda sola (el tren duplicado que se mueve) o se queda quieta si le sacaron el conductor. Mientras tanto, `FollowerWatchdog` o `HandleState` reconstruyen la copia buena. Además el client no deja ningún log de trenes: `logger::info` va a stdout.

**T82 (NPCs invisibles).** `CNetworkPed::CreateGamePed` (`CNetworkPed.cpp:244`) devuelve false sin log en varios casos. El más sospechoso es el de la línea 268: el slot especial (290-299) del invitado tiene otro personaje con `m_nRefCount > 0`. El host reutiliza slots (Sweet en SPECIAL01 y después Ryder en el mismo slot). Si en el invitado sigue viva una copia o un ped que usa el modelo viejo de ese slot, la copia de Ryder no se crea nunca. Esto explicaría tanto lo de Ryder como Sweet en el aire, si en la cinemática se reemplaza un ped de script.

## Orden de la cadena
T80, T81, T82, T83, T84, T85, T86, T87, en serie (comparten archivos). Una tarea por commit. Ninguna instala ni abre los GTA.
