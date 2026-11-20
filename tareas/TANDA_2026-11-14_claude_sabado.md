# TANDA 2026-11-14 — pedidos del sábado (`promtpssabados.md`)

Orquesta Claude (Opus). Cinco subagentes Sonnet, cada uno en su worktree y rama `tanda/2026-11-14-<grupo>`; Claude revisa, integra en `main`, sube el protocolo una sola vez y compila/testbot. Nadie instala ni abre los GTA (el dueño está testeando).

| Grupo | Worktree | Pedidos | Archivos probables |
|---|---|---|---|
| ui | `wt-ui` | menú de oferta VS arriba a la izquierda estilo SA; sacar el cartel viejo de posición/vueltas abajo a la derecha; menú de chat de voz (micrófono, volúmenes) | `CVsLobby.cpp`, `CVsRace.cpp`, `CMinigameVs.cpp`, `CVoice.cpp`, UI |
| mision | `wt-mision` | Nines and AKs: audio que se bugea al hablar y diálogos repetidos; fin de la primera parte de Emmet: invitados cerca del auto del host y suben con animación; Drive-By: no dar 1000 balas de Tec9 a los invitados | `CMissionSyncRules.cpp`, reglas de misión, audio de misión |
| tienda | `wt-tienda` | tienda de ropa: si matan a la empleada la misión falla; trucos visibles para todos (y que siempre se vea); animaciones de a dos F9 siempre "rechazado" | `CCheatSync.cpp`, `CPlayerAnimMirror.cpp`/F9, reglas de misión |
| recluta | `wt-recluta` | todos pueden reclutar Grove (no solo el host); paramédico coop: heridos suben a ambulancias de invitados aunque el que inició esté lejos | reclutas, paramédico |
| barcos | `wt-barcos` | sincronizar barcos | sync de vehículos |
| Claude (main) | — | "R" de Ryder sin función tras la misión Ryder (Home Invasion recién al llegar a Drive-By); cartel de espera que parpadea en el host; mensajes arriba a la izquierda (sobre todo "esperando a los jugadores") | `CMissionSyncRules.cpp` (lanzadores), `CMissionHints.cpp` |

Reglas para todos: `AGENTS.md`; CRLF en `.cpp/.h` (Python `newline=''`); interruptor nuevo por sistema; gta-reversed antes de parchear; paquetes nuevos al final de `packet_types.h` **sin subir la versión** (la sube Claude al integrar); pruebas y notas de interruptor en la sección del grupo abajo, no en `TESTEAR_HOY.md`/`INTERRUPTORES.md` (las pasa Claude).

## Casos borde (todos)
- 3-4 jugadores; uno en individual (F1); desconexión o muerte a mitad; interior u otra punta del mapa; interruptor apagado = como antes.

## Resultados por grupo
(cada subagente agrega acá: qué hizo, commits, interruptor, pruebas `- [ ]`, qué no pudo)
