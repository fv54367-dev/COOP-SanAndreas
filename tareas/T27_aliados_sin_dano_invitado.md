# Tarea T27_aliados_sin_dano_invitado: un invitado no lastima ni enoja a los aliados de la misión
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. Interruptor nuevo `mission-ally-no-guest-damage` (por defecto `true`; apagado = comportamiento actual; documentalo en `INTERRUPTORES.md`).

## Problema (probado en juego)
Cleaning the Hood: si un invitado le pega a Ryder, Ryder queda enojado con todos, host incluido. Debe valer para todas las misiones con NPC aliados, sin romperlas.

## Cambio
Descartá, en la PC donde corre la IA del ped, los eventos `EVENT_DAMAGE` (9) y `EVENT_GUN_AIMED_AT` (31) cuando se cumple todo esto:
- Hay una misión de historia coop en curso y `CMissionSyncRules::IsEnabled()`.
- La fuente del evento es el ped de un jugador de red, o el jugador local si esta PC no es la autoridad del script (un invitado puede ser dueño del ped por traspaso). Los golpes del host/autoridad no cambian: son como en el juego original.
- La víctima es un aliado de misión: `m_nCreatedBy` de misión (2), no es jugador, y además (es miembro del grupo de un jugador, o su acquaintance POR PED tiene PLAYER1 en Respect o Like y no lo tiene en Dislike ni Hate). Usá la acquaintance del ped, no la del tipo: SWEET1B pone Respect para todo `Mission1` y después `Char.SetRelationship(x, Dislike, Player1)` a los enemigos, que también son `Mission1`.

Elegí el punto de enganche mirando gta-reversed (`CEventDamage::AffectsPed`, vtable en `0x85AD8C`, en `client/src/Hooks/DamageHooks.cpp` ya están los sitios comentados; `CEventGroup::Add` ya tiene hook en `CrashfixHooks.cpp`). El evento descartado no debe hacer daño ni generar reacción. Citá la función de gta-reversed en un comentario corto. Log `rule-ally-protected` cada 5 s como máximo.

## No cambia
Enemigos, peds comunes de la calle, peds de pandilla que no son de misión, misiones individuales.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤6 líneas.
