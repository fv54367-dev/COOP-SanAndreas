# Tarea T65: las fotos del invitado cuentan (Badlands, Photo Opportunity, Architectural Espionage)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y lo que dejaron **T63** (`mission-guest-counts`) y **T64** en la tanda y en sus commits: esta tarea se apoya en ellos. No toques `main.scm` ni `scm/`: C++ sobre scripts originales.

## Problema (auditoría del Codex del amigo, 2026-11-10; verificalo)
El script del dueño pregunta si **su** cámara fotografió el objetivo; la foto del invitado no le llega.
- Badlands (fotografiar el cadáver del reportero).
- Photo Opportunity (fotografiar a los objetivos de los Loco Syndicate).
- Architectural Espionage (fotografiar los planos: el script consulta el objeto del dueño).
Pista: control de fotografías en `client/src/Hooks/PlayerHooks.cpp` ~353. Los scripts archivados (`scm/scripts/*.txt`, solo para leer) sirven para ubicar las secuencias en el `main.scm` original; no son el modo de juego.

## Referencia
Buscá en `_refs\sa-coop-karimov` cómo lo resuelven (grep `PHOTO`, `IS_CHAR_IN_ANGLED_AREA`/`camera`/`photo`, `HAS_..._BEEN_PHOTOGRAPHED`, mission-rules `kSpots`). Si no lo tienen, diseñalo con `_refs\gta-reversed` (`CWeapon::TakePhotograph`, flags de entidad fotografiada).

## Cambio
Interruptor `coop-photos` (cliente, default **true**, `INTERRUPTORES.md`). Apagado: como hoy.
- Cuando el invitado coop en la misión saca una foto, su juego calcula qué entidades de misión quedaron fotografiadas (con la regla del propio juego) y se lo manda al dueño, que marca la entidad **original** como fotografiada en su juego (una sola vez). El script del dueño sigue solo.
- Sistema general (cualquier misión que pregunte por fotos), no tres parches.

## Casos borde
- 3–4 jugadores, dos fotos a la vez del mismo objetivo: cuenta una.
- Entidad de misión sin copia en el invitado: no se puede fotografiar ahí (decilo).
- Invitado en individual o fuera de la misión: no cuenta.
- Fotos fuera de misión (coleccionables de San Fierro): no se tocan (ya tienen `CCollectibleSync`).

## Aceptación
Compilan client/server/testbot (versión si hay protocolo). CRLF. 1 prueba simple por misión en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda. No instales. Resumen ≤10 líneas.
