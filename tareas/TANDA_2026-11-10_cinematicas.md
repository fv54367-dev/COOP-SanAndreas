# Tanda 2026-11-10 CJ propio en las cinemáticas (GLM a mano)

Sigue a `TANDA_2026-11-10_sync_visual.md`. Nace de una investigación de solo lectura pedida por el dueño (2026-11-10) sobre el bug de las cinemáticas.

## T48: cada jugador ve su propio CJ en las cinemáticas
Síntoma: en las cinemáticas el host tomaba el CJ del invitado y viceversa; la ropa salía bugeada y el cuerpo deforme (el buzo "grande" se veía flaco). Con el workaround del CJ estándar (2026-10-21), el host veía el estándar y el invitado seguía viendo el CJ del modo libre del host.

Causa (gta-reversed `ClothesBuilder.cpp`, `CreateSkinnedClump` 0x5A69D0): el CJ de cinemática (MODEL_CSPLAY) se skinea desde el TXD `player` compartido, y la reconstrucción de texturas está guardada por `!bCutscenePlayer`: para cinemáticas NUNCA se regeneran, se usan las que dejó el último `CClothes::RebuildPlayer` de esa máquina. En coop el ped remoto se viste por la misma cañería (`CNetworkPlayer.cpp:96` al crearse, `CPersistenceSync.cpp:83` al sincronizársele ropa), así que el TXD suele quedar con las texturas del OTRO jugador: cada máquina mostraba el CJ del otro, con la geometría blendada por las stats locales sobre texturas ajenas (de ahí el cuerpo deforme). El juego nunca limpia esto antes de una cinemática: `CClothes::RebuildPlayerIfNeeded` (0x5A8390) solo dispara si cambiaron grasa/músculo. Dato: el CJ "estándar" que veía el host era la ropa default del ped remoto (`CNetworkPlayer.cpp:26-29`), no el workaround funcionando.

Cambio: `CutsceneClothesHooks` fuerza un `CClothes::RebuildPlayer(pedLocal, false)` justo antes de llamar al `RebuildCutscenePlayer` vanilla (0x5A8270, call sites 0x4D5E43 y 0x4D5EA3); ese rebuild sí regenera las texturas del TXD con la ropa y stats del local, y el CJ de cinemática sale con lo propio (decisión del dueño 2026-10-21: cada uno ve su CJ). El juego mismo corre `RebuildPlayer` en ese mismo punto cuando cambian las stats, o sea que el timing ya estaba probado. Se borran el workaround del CJ estándar y el fix deshabilitado del descriptor del authority (2026-10-21).

Archivos: `client/src/Hooks/CutsceneClothesHooks.{cpp,h}`. Sin protocolo nuevo (sigue 0.4.21-alpha), sin interruptor (el sistema no lo tenía).

Casos borde:
- El remoto se cambia de ropa durante la cinemática: su rebuild toca el TXD, pero CSPLAY ya está skineado con sus propias referencias (las texturas de RenderWare son refcounted); no se corrompe.
- Fat/muscle distintos por jugador: cada máquina usa sus stats globales locales; correcto porque el CJ de la cinemática es el propio.
- Ped local sin PlayerData: el hook no fuerza el rebuild y cae al vanilla.
- `RebuildPlayer` remueve y reincorpora al ped del mundo: lo mismo que hace el juego al comprar ropa o cambiar grasa/músculo, incluido este call site vía `RebuildPlayerIfNeeded`.

Aceptación: compila; en juego, cada uno ve su propio CJ y ropa en las cinemáticas (pruebas 1-3 en `TESTEAR_HOY.md`).

## Estado
Compilado e instalado el 2026-11-10 (client 30 s, server 8 s; DLL local sellado 2026-11-10.1, el CI sella los paquetes de GitHub). SIN PROBAR en juego. Falta la revisión cruzada de Codex.
