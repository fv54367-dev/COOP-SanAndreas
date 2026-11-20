# Tarea T63: lo que hace el invitado dentro de la misión también cuenta
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`, `docs/PLAN_CPP.md` y `docs/MISIONES_MODO.md`. **No toques `main.scm` ni `scm/`**: es todo C++ sobre los scripts originales (`vanilla-scripts=true`).

## Pedido del dueño (2026-11-10)
El script de la misión corre en el dueño y pregunta siempre por **su** CJ ("¿lo hirió CJ?", "¿CJ dispara?", "¿CJ apunta a este?", "¿CJ está al lado del objeto?"). Lo que hace un invitado no cuenta. Que cuente, sin romper nada: "si no rompe las cosas, agregalo".

## Referencia (MIT, portar)
`_refs\sa-coop-karimov\src\client\features\story\mission-rules\MissionRules.{h,cpp}`. Ellos, en el juego del host, después de cada condición del script repiten la pregunta para la copia de cada otro jugador (el handler del propio comando corre otra vez con `$PLAYER_ACTOR` apuntando a la copia), y si alguno la cumple la respuesta es "sí":
- en todas las misiones: `HAS_CHAR_BEEN_DAMAGED_BY_CHAR`, `HAS_CAR_BEEN_DAMAGED_BY_CHAR`, `IS_CHAR_SHOOTING`, `IS_CHAR_SHOOTING_IN_AREA` del jugador, e `IS_PLAYER_TARGETTING_CHAR` según lo que los compañeros dicen que apuntan;
- líneas sueltas (`kSpots`) por su lugar en el `main.scm` original y su opcode: solo las que lo que sigue es seguro para un jugador que está en otro lado (si lo que sigue mueve a CJ, no se lista);
- tramos más largos que "van para el otro jugador" con `BeforeCommand`/`SkipCommand`/`AfterCommand`; caso especial "Catalyst" (cajas desde el tren, `Throw`).
Leé también dónde enganchan el hook de comandos del script (grep `rules::` en su repo).

## Lo que ya hay acá (investigá primero y no dupliques)
- T29 enemigos que atacan a cualquier jugador, `CMissionSyncRules`, `Hooks/DamageHooks.cpp`, `CMirrorPickups` (el invitado agarra pickups de misión y avanza la del dueño), el sync de daño de NPC (T46: una sola autoridad para el daño), `COpCodeSync`.
- Nuestro "dueño de la misión" no es el host del server: es quien corre el script (`IsScriptAuthority` / `CCoopMission`). Usá eso, no "host".

## Fase 1 (esta tarea)
- Las preguntas genéricas de daño, disparo y apuntado (lista de arriba), para todos los invitados **coop que están en la misión** (no individuales, no los que no participan).
- `kSpots`: portá solo las líneas cuya continuación no mueva al CJ del dueño ni lo meta/saque de un auto; si una línea es dudosa, afuera. Listá en la tanda cuáles portaste.
- Tramos largos y Catalyst: **solo si entran limpios**; si no, fase 2 en la tanda.

## Reglas que no se rompen
- El marcador sigue esperando a todos; nada de "uno arranca para todos".
- Dinero, estrellas y progreso de cada uno.
- Una respuesta "sí" por un invitado nunca teletransporta, congela ni controla al CJ del dueño.

## Interruptor
`mission-guest-counts` (cliente del dueño, default **true**, en `INTERRUPTORES.md`). Apagado: exactamente lo de hoy.

## Casos borde
- 3–4 jugadores: se pregunta por cada uno; alcanza con que uno cumpla.
- Invitado en individual, muerto, desconectado, en interior distinto o sin copia: no cuenta.
- Daño: no doble conteo con T46 (la autoridad del daño ya es una).
- Misiones con scripts modificados (offsets distintos): `kSpots` no aplica (por eso van por lugar en el `main.scm` original); las genéricas sí.

## Aceptación
Compilan client/server/testbot (versión si hay protocolo). CRLF. Cita MIT. En `TESTEAR_HOY.md`, 3–4 pruebas simples por misión (ej.: "Sweet & Kendl / Drive-Thru: el invitado mata a los Ballas y la misión avanza"). `INTERRUPTORES.md`, resumen y lista de líneas en la tanda. No instales. Resumen ≤10 líneas.
