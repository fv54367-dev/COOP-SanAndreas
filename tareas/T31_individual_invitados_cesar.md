# Tarea T31_individual_invitados_cesar: los invitados pueden jugar solos, y Cesar Vialpando es individual para todos
Leé `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-09_claude.md` y `docs/MISIONES_MODO.md`. Si necesitás protocolo, la versión ya es 0.4.19-alpha (T25): no la subas de nuevo. No toques `main.scm`.

## Problema (probado en juego)
1. El modo individual (F1) no funciona para los invitados: no les deja iniciar misiones.
2. Cesar Vialpando (misión 19, contador de Sweet 6, script `SWEET6`) solo la puede iniciar el host; los invitados no, ni en coop ni en individual.

El camino de `FilterMissionStart` (`client/src/CMissionSyncRules.cpp:114`) parece permitirlo (`bAlone` devuelve verdadero), así que la traba puede estar en otro lado. No asumas: seguí el camino completo de un invitado desde el marcador hasta `LOAD_AND_LAUNCH_MISSION` y la misión corriendo.

Hipótesis a verificar:
- `$onmission` del invitado queda en 1 por `OnMissionFlagSync` / `ClearHostMissionState` (`PacketHandlers/scripts.cpp`) aunque esté en individual, y el lanzador exige `$onmission == 0`.
- `bIndividualOnly` usa el contador propio del invitado: si su contador no vale 6 cae en la rama coop y `!IsScriptAuthority()` lo bloquea.
- `IsScriptAuthority()` es falso para un invitado que juega solo, y alguna regla, mute o `CMissionMirror` (`IsLocalMissionRunning`, `IsMirrored`, `MirrorPolicy`) trata su misión local como si fuera la del host.
- `CMissionMode::SetIndividual` o `LocalCleanup` le dejan algo trabado.

Datos del log real del invitado (última sesión, build 2026-11-08.1): arrancó secundarias solo sin problema (`mission-start id=124 ... side-release script=COPSOLO synced=0`), y no hay NINGÚN intento registrado en el marcador de Sweet en scripts originales, porque `FilterMissionStart` no loguea. Los `sweet-entry` con `counter=6 alone=1` del mismo archivo son de sesiones viejas con scripts coop (archivados): ahí el invitado sí llegó a `mission-start id=19`. El server ya no le reenvía `ON_MISSION_FLAG_SYNC` al que está en individual según `CCoopMission::Relay`: confirmalo antes de seguir esa hipótesis.

**Pedido expreso del dueño: si no encontrás una causa demostrable en el código, o si arreglarlo a ojo puede romper otra cosa, NO cambies el comportamiento.** En ese caso agregá solo el log `solo-launch` y el aviso de Cesar, y explicá en el resumen qué falta saber.

## Cambio
- Un invitado en individual (F1) inicia cualquier misión de historia en su marcador, como el host en individual, sin que le lleguen ni le frenen las cosas de la misión del host.
- Cesar Vialpando: cualquier jugador, en coop o individual, la inicia solo. Al arrancar, mensaje una vez: "Esta mision es individual: la jugas solo." Sigue valiendo el bloqueo de `CMissionLock`: de a uno por vez; el que llega mientras otro la juega ve quién la está jugando y que espere a que termine.
- Log `solo-launch` con `script`, `counter`, `onmission`, `authority`, `individual` en cada intento del marcador (cada 5 s como máximo), para poder diagnosticar desde el log del invitado.

## No cambia
Fuera de alcance: que los demás VEAN la misión individual de otro. No cambies `COpCodeSync::IsLocalOnlyMissionRunning` ni la regla de actores locales. Una misión coop sigue arrancando con todos en el marcador y solo el dueño la corre.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md`, actualizá `docs/MISIONES_MODO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤8 líneas, con qué causa encontraste.
