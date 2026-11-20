# Tarea T82: el invitado no ve NPCs de misión (Ryder, Sweet)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md` (hipótesis T82). Código:
- `client/src/CNetworkPed.cpp` (`CreateGamePed`, :244)
- `CNetworkPedManager.*`
- `client/src/Hooks/PedHooks*` (`ms_aszLoadedSpecialModels`)
- lo que crea copias de peds en cinemáticas

## Qué reportó el dueño
- Cleaning the Hood (`SWEET1B`): apenas arranca la misión el host ve a Ryder y el invitado no.
- Tagging up Turf (`SWEET1`), cinemática final: Sweet no aparecía ("literalmente aire").
- En general el invitado no ve a los NPCs de misión. El dueño ofrece correr una misión en vivo: dejá logs que alcancen para diagnosticarlo con esa corrida.

## Qué hacer
1. **Log** de cada motivo por el que una copia de ped no se crea o se borra. Formato: `ped-copy-fail reason=... model=... special=... slot=... refs=...`, una vez por ped y motivo, no por frame.
2. **Hipótesis de Claude:** el slot especial está ocupado por otro personaje con referencias (`CNetworkPed.cpp:268`).
   - Verificala con el código y con gta-reversed: `CStreaming::RequestSpecialModel` 0x409D10, `CModelInfo`, `LOAD_SPECIAL_CHARACTER` 023C.
   - Si es así, arreglalo de raíz. Por ejemplo: la copia usa cualquier slot especial libre del invitado que tenga o pueda cargar ese nombre (mapeo nombre→slot local), en vez de exigir el mismo número de slot que el host.
   - Las copias viejas que usan un slot que el host ya reasignó se recrean con el modelo nuevo.
3. Si encontrás otra causa, arreglala y documentala. Candidatas:
   - distancia, área o interior
   - filtro de peds de script en el invitado
   - `interior-mission-ped-keep`
   - `dup-ped` que borra la copia buena

## Casos borde
- Slots 290–299 todos ocupados.
- El script local del invitado también cargó especiales.
- Ped especial en una cinemática.
- 3–4 jugadores.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (Ryder en Cleaning the Hood, Sweet al final de Tagging up Turf) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
