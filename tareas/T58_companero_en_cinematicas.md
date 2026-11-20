# Tarea T58: el compañero aparece dentro de las cinemáticas
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y `tareas\TANDA_2026-11-10_cinematicas.md` (T48: cada jugador ve su propio CJ). No toques `main.scm` ni `scm/`.

## Pedido del dueño
En una cinemática de misión coop, los otros jugadores aparecen en la escena al lado de CJ (parados, apoyados en una pared, sentados), escuchando como un actor más, en el mismo lugar en todos los juegos.

## Referencia (MIT, portar)
- `_refs\sa-coop-karimov\src\client\features\story\cutscene-partner\ScenePartner.{h,cpp}` (~3.300 líneas), `cutscenes.txt` (lugares a mano), `cutscene_ways.txt` (caminos precalculados, recurso embebido vía `coop.rc`).
- `_refs\sa-coop-karimov\src\client\features\story\cutscene-clips\SceneMocap.{h,cpp}`: clips de "escuchar" recortados en la PC del jugador de `anim\cuts.img` a un bloque propio (`COOPCS`); el mod no distribuye datos de animación.
- Cómo lo usan: `Prepare` al cargar la cinemática, `Analyse` desde el render hook, el host manda la decisión por toma (`TakeDecision`), los demás `FollowSpot`/`Follow`, `ForRank` para el 2.º–4.º, `Hold` cada frame, `ReleaseAll` al terminar. Buscá en su repo dónde se llaman (grep `scene::`).

## Alcance de esta tarea (fase 1)
- Poses `STAND`, `LEAN`, `SIT`, `STEP`, `HIDDEN` y `WALK` (al lado de CJ), con los clips de escuchar, más `cutscenes.txt`.
- `POSE_WAY` (caminos de `cutscene_ways.txt`) y `POSE_CAR` (asientos de autos de la escena): **portalos también si entran limpios**; si no, dejalos para una fase 2 y decilo.
- Quién decide: el **dueño de la misión** (el que corre la cinemática y manda la cámara por `CCameraMirror`), no "el host del server".

## Reglas nuestras
- La copia del compañero en la cinemática usa **la ropa de ese jugador** (como T48). Nada de progreso ni dinero.
- Interruptor `cutscene-partners` (cliente, default **true**, `INTERRUPTORES.md`). Apagado: cinemáticas como hoy.

## Casos borde
- 3–4 jugadores: el 1.º en el lugar elegido, los demás al lado (`ForRank`), iguales en todos los juegos.
- Jugador en individual, lejos o que no está viendo la cinemática coop: no aparece.
- Cinemática saltada (todos de acuerdo), o el dueño se desconecta en medio: `ReleaseAll` y nada queda congelado ni con props.
- Cinemáticas sin CJ o marcadas `hidden`: no aparece nadie.
- `cuts.img` modificado/no legible: sin clips (pose base), sin crash.

## Aceptación
Compilan client/server/testbot (versión subida si hay paquetes). CRLF. Cita MIT y entrada en `NOTICE.md`. `TESTEAR_HOY.md` (2–3 cinemáticas concretas para probar), `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.
