# Tarea T76: trenes duplicados en el invitado (T62)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md` y `tareas\T62_fix_trenes_glm.md`. Código: `client/src/CTrainSync.*`, `client/src/CPatch.cpp` (TemporaryPatches / RevertTemporaryPatches*), `client/src/PacketHandlers/system.cpp:204`, `server/src/PacketHandlers/train.cpp`. No toques `main.scm` ni `scm/`.

## Qué reportó el dueño (2026-11-10, PROBAR_SOLO, 2 jugadores, `train-sync=true`)
"Se ven los mismos trenes y se puede subir y bajar, pero parece que se duplican: en una estación llegaron 2 trenes a la vez y se atravesaban (los 2 interactuables); en el camino a veces aparecían otros." El diseño de T62 es **un solo juego de trenes, el del host**: el invitado no tiene trenes random propios, solo copias.

## Pista importante
Hasta el commit b247f59 (2026-11-10) `Events::initGameEvent` no se ejecutaba nunca (lo pisaba `PatchSCM`): `CPatch::TemporaryPatches` (que hace `CTrain::DisableRandomTrains(true)`) no corría en las pruebas anteriores. Revisá cómo interactúan ahora TemporaryPatches, `RevertTemporaryPatchesForHost`, `OnBecameFollower` y la reconexión, y en qué momento puede quedar un tren random propio en el invitado (antes de recibir el rol, al cargar partida, al reconectar, generación nativa en `CTrain::DoTrainGenerationAndRemoval`, trenes de misión del script). No hay eventos de tren en `E:\juegos\gtasacoopnuevo\_logs_2026-11-10\*mirror.log`: agregá un log `train-local-random` (una vez por tren, en el que no es host) para que la próxima prueba lo muestre.

## Casos borde
- 3–4 jugadores; el host cambia (se desconecta) y otro pasa a host: el nuevo host vuelve a generar trenes, los demás los borran.
- Tren de misión (script) del dueño: no borrarlo; el invitado que lo ve tiene que ver la copia.
- Jugador subido a un tren random cuando se borra: bajarlo sin crash.
- `train-sync=false`: exactamente el comportamiento anterior a T62.

## Aceptación
Causa con evidencia (gta-reversed `Train.cpp`: `DoTrainGenerationAndRemoval`, `DisableRandomTrains`, `RemoveOneMissionTrain`), arreglo de raíz, compilan client/server/testbot, CRLF. Prueba simple en `TESTEAR_HOY.md` (sección trenes) y en "Pendiente" de `ESTADO.md`. Sin protocolo nuevo si se puede. No instales. Resumen ≤10 líneas.
