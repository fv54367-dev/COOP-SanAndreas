# Tarea T69: Home Invasion con el diseño de sa-coop (una sola caja activa, la lleva cualquiera)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\T64_home_invasion_coop.md`, `tareas\TANDA_2026-11-10_codex_t64.md` y **`tareas\TANDA_2026-11-10_codex_t68.md`** (por qué T68 se detuvo: la entrega serializada no cierra). No toques `main.scm` ni `scm/`.

## Cambio de diseño (decisión del orquestador, 2026-11-10)
**Descartá la cola de entregas de T68.** Se porta el modelo de sa-coop (MIT), que no necesita serializar nada:
- GUNS1 sigue con **una sola caja activa** (36@/60@), como en el original. La puede levantar el dueño **o un invitado**, uno por vez.
- Cuando un invitado levanta la caja, en su juego la lleva su CJ (copia local); **en el juego del dueño la caja real pasa a las manos de la copia de red de ese invitado** (no la del CJ del dueño). Ningún otro escribe la posición de esa caja mientras la lleva alguien (resuelve el punto 4 de T68: dos escritores).
- Las preguntas del script sobre su CJ se contestan por el portador:
  - `IS_CHAR_HOLDING_OBJECT` (0x0737) y `LOCATE_CHAR_ON_FOOT_OBJECT_3D` (0x0475) de `$PLAYER_ACTOR` con la caja → "sí" si la tiene un invitado. Con esto el propio script pone 60@=1 y llega solo a BD1B0/BD1DE (resuelve el punto 1 de T68).
  - `LOCATE_CHAR_ON_FOOT_3D` de la furgoneta (0x00FF, en GUNS1, con los bytes exactos del comando) → "sí" solo si **el portador** está dentro del área; si la lleva un invitado, la respuesta es la suya, no la del dueño.
  - El botón de cargar que sigue (`IS_BUTTON_PRESSED 0 15`, 0x00E1) → "sí" si el portador apretó entrar hace menos de 1 s (el invitado lo manda).
  - El resto del tramo de entrega (BD271–BD697) corre igual que siempre: actualiza contadores, Ryder y borra la caja; el invitado suelta su copia con la tarea nativa (`DropEntityThatThisPedIsHolding`, gta-reversed 0x5E0360).
- Respuesta cambiada **por el camino del propio juego** (`UpdateCompareFlag` después de restaurar el estado de condición), para que NOT y las cadenas AND/OR salgan bien. Reusá el gancho de T63 (`CMissionGuestCounts`/`FilterCompareFlag`).

## Referencia (MIT, portar)
- `_refs\sa-coop-karimov\src\client\features\story\missions\Missions.cpp`: `CrateAnswer`, `kGunsTruck` (bytes del LOCATE de la furgoneta), `MissionLocal`, `SendHint("RYD1_30")`; ropa: `SendMissionLook`/`ApplyMissionLook`/`RestoreOwnLook`/`HandleMissionLook` (balaclava + marcha sigilosa); ruido: `UpdateOwnNoise` (~1421).
- `_refs\sa-coop-karimov\src\client\features\mission-objects\Objects.cpp`: `Held`, `TakeUp`, `HandleHold`, `FollowHolder`, `PartnerHolds`.
Adaptá "host" → **dueño de la misión** (`CCoopMission::IsScriptAuthority`).

## Interruptor y reglas
`coop-home-invasion` (el de T64). Apagado: como hoy. Dinero/progreso de cada uno; la ropa de misión se saca al terminar/fallar/abandonar (vuelve la propia). Si cae cualquiera, falla para todos (regla).

## Casos borde
- Dos que quieren la misma caja: el dueño de la misión decide (el primero que confirma).
- El portador invitado se desconecta, muere o pasa a individual con la caja: la caja cae en su lugar y vuelve a poder levantarse.
- 3–4 jugadores: uno lleva la caja; los otros vigilan/hacen ruido con su barra.
- Fin/reintento: caja, blips, ropa y ruido limpios en todos.

## Aceptación
Compilan client/server/testbot (si hay protocolo, versión siguiente a la de `shared/config.h`; paquetes al final). CRLF. Cita MIT. Las 4 pruebas de GUNS1 en `TESTEAR_HOY.md` (invitado levanta y carga una caja; dueño carga otra; balaclava y sigilo en el invitado; ruido del invitado despierta al coronel), `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda. Si aun así algo no cierra, implementá lo que sí (ropa y ruido son independientes) y explicá el resto. No instales. Resumen ≤10 líneas.
