# Tarea T84: volvió el CJ cruzado en cinemáticas después de cambiarse ropa (regresión de T48)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_cinematicas.md` (T48) y `tareas\TANDA_2026-11-10_pruebas_b.md`.

Código:
- `client/src/Hooks/CutsceneClothesHooks.*`
- `CPersistenceSync.cpp` (ropa del remoto)
- `CNetworkPlayer.cpp` (vestir al remoto)
- gta-reversed: `ClothesBuilder.cpp`, `Clothes.cpp`, `CutsceneMgr.cpp`

## Qué reportó el dueño
El arreglo de T48 andaba: cada uno veía su propio CJ en las cinemáticas. Después de cambiarse ropa o peinado (tienda o barbería) vuelve a pasar: el host ve el CJ del invitado y el invitado el del host.

## Qué hacer
Encontrá por qué el `RebuildPlayer(local)` del hook deja de alcanzar después de un cambio de ropa. Posibles causas a revisar:
- Otro camino arma el CJ de cinemática: por ejemplo, `RebuildCutscenePlayer` llamado desde otro sitio, o `bCutscenePlayer` armado al salir de la tienda.
- El rebuild del remoto que dispara el cambio de ropa del otro pisa el TXD `player` después del hook.
- Algo queda cacheado en la tienda (`CClothes`, `CShopping`).

Arreglalo de raíz y agregá un log `cutscene-cj-rebuild` que diga con qué ped se armó.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (cambiarse ropa y peinado, después ver una cinemática) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
