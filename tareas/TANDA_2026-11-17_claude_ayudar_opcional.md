# TANDA 2026-11-17 — Claude: misiones de historia por jugador (ayudar opcional + íconos propios)

**Pedido del dueño (17-11):** un invitado que ya terminó una cadena (ej. César) ve el ícono; el host entra al marcador, al invitado le sale "Te esperan", se acerca y la misión le arranca aunque ya la hizo. Decisiones del dueño:
1. **Ayudar opcional:** si un invitado NO tiene pendiente la misión que arranca el host (ya la hizo o todavía no llegó), no se lo espera ni se lo mete a la fuerza: le sale "¿Ayudar a <host> en <misión>? Y/N". Si acepta, juega como ayudante (no se le guarda nada, como hoy). Si no acepta o no contesta, sigue libre en el mundo. Los que sí la tienen pendiente se esperan como hoy.
2. **Íconos propios:** cada jugador ve los contactos de SU partida (no los del host).

## Causa encontrada (17-11)
- `CMissionMode::LauncherChain` no tiene `CESAR` (cadena 5, `$flag_cesar_mission_counter` +1828) y el arranque de High Stakes va por `CHighStakesRace` (`AuthorityWait START`) sin chequear progreso: por eso arrancó para el invitado que ya la había hecho. Con las demás cadenas `FindChainMismatch` bloquea el arranque para todos ("Progreso distinto... F1").
- `contacts-follow-host` (15-11, `CNetworkStaticBlip.cpp`) hace que el invitado vea los contactos del host. Se había puesto porque el script de misión corre solo en el host y es él el que hace `ADD_SPRITE_BLIP_FOR_CONTACT_POINT`/`REMOVE_BLIP xxx_contact_blip` al aprobarse (`sweet7.sc:5808`): los íconos del invitado nunca se actualizaban.

## Diseño
Interruptor nuevo `story-help-optional` (default true; apagado = como hoy). `contacts-follow-host` pasa a default **false**.

**A. Clasificación al arrancar** (autoridad, en el gate de `CMissionRulesCompare.cpp` ~l.145 y en el arranque de `CHighStakesRace.cpp` ~l.440): para cada jugador incluido, comparar su `m_anChainProgress[chain]` (ya es el propio, `OwnChainCounter`) con el contador de la autoridad. Agregar `{"CESAR", 5}` a `LauncherChain`.
- Igual → **pendiente**: se espera como hoy.
- Distinto (y no `CHAIN_UNKNOWN`) → **no pendiente**: no entra en `AreMissionPlayersNear`/`missing`; la autoridad le manda una oferta (paquete nuevo autoridad→server→jugador, al final de `packet_types.h`, subir versión en `shared/config.h`). Con `story-help-optional` encendido, `FindChainMismatch` deja de bloquear el arranque.
- El invitado no pendiente ve "¿Ayudar a X en <misión>? Y/N" (texto en español, nombre con `GetChainStateName`), con timeout ~12 s. Reusar el estilo de `CCoopMission` (Y/N, `CHOICE_TIMEOUT_MS`), no su estado de server.
  - **Y** → respuesta a la autoridad; pasa a contarse como incluido/esperado (como hoy). Es ayudante: no se le aplica `CStoryProgress::Apply` ni los contactos de B.
  - **N/timeout** → queda afuera **solo durante esa misión**: la forma más segura es que pase a modo individual temporal (el mismo camino que F1, `CMissionMode::ms_bIndividual` + su paquete de modo) y vuelva solo cuando termina la misión de la autoridad (o se desconecta / cambia el host). No mostrar "Te esperan" a quien no está pendiente.
- Si el invitado se acerca al marcador sin oferta abierta y no la tiene pendiente: cartel "Ya hiciste/no llegaste a <misión>: esperá la invitación para ayudar" en vez de "Te esperan".

**B. Íconos propios** (`contacts-follow-host=false`): al aprobar una misión de historia, la autoridad junta qué blips `BLIP_CONTACTPOINT` (no de misión ni del espejo) creó y sacó durante la misión (foto al lanzar vs al terminar, mismo patrón que `CStoryProgress`) y lo manda junto con el progreso. Un receptor que era **pendiente** aplica ese cambio en sus propios contactos (sacar el suyo en la misma posición/sprite, crear los nuevos); un ayudante no aplica nada. Como la misión terminada juntos cuenta para el pendiente, esto sí se guarda (es su progreso). Respetar `GenericSave_OwnProgress` para lo que sea copiado (lanzadores/contadores de ayudantes).

## Casos borde
3-4 jugadores (cada invitado se clasifica solo); host solo pendiente con todos ayudantes que dicen N (arranca solo); `CHAIN_UNKNOWN` (cliente viejo) = como hoy; desconexión durante oferta; cambio de host; Cesar Vialpando (Sweet 6) sigue individual como hoy; misiones secundarias (`CCoopMission`) sin cambios. No tocar Big Smoke ni Cleaning the Hood (memoria del dueño: andan bien) más allá de lo general.

**Archivos probables:** `CMissionRulesCompare.cpp`, `CHighStakesRace.cpp`, `CMissionMode.{h,cpp}`, `CMissionHints.cpp`, `CStoryProgress.{h,cpp}`, `CNetworkStaticBlip.{h,cpp}`, `CConfigLoader.{h,cpp}`, `shared/network/packet_types.h` + `packets/scripts.h`, handlers cliente y server, `shared/config.h`, `INTERRUPTORES.md`.

**Estado:** A y B implementados en un worktree (client, server, testbot compilan, protocolo 0.4.57-alpha); SIN PROBAR. Nuevo: `client/src/CStoryHelp.{h,cpp}`, `shared/network/packets/story_help.h` (`STORY_HELP`, `STORY_CONTACTS`).
Revision Claude 17-11: fix del delta de contactos que llegaba al ayudante despues del END (ignora 10 s), y `story-help-optional` manda sobre `contacts-follow-host=true` de ini viejos. Rev ok.
