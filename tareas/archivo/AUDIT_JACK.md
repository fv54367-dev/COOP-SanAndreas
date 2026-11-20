# Auditoría (SOLO LECTURA): robo de vehículo a un NPC, sync de "jacked"

Repo: `E:\juegos\gtasacoopnuevo\coopandreas-server`. **NO modificar ningún archivo del repo.** Escribí el informe en `E:\juegos\gtasacoopnuevo\tareas\AUDIT_JACK_informe.md` (≤ 60 líneas, español, con `archivo:línea`).

## Síntoma (probado por el usuario con 2 jugadores, en ambos sentidos)
Cuando un jugador (CJ) le roba el auto a un NPC conductor: CJ hace la animación de sacarlo, pero el NPC NO hace la animación de ser arrastrado fuera del auto; simplemente aparece afuera, al lado o arriba del auto, y se va caminando como si no pasara nada (no huye, no pelea, "nunca se entera"). Esperado: el NPC reproduce la animación de ser sacado y después reacciona (huir/pelear) como en el juego normal.

## Qué mirar
- `client/src/CJackSync.cpp/.h` (flujo completo: detección local del robo, paquete `PED_JACKED` / `PlayerJacked`, `ApplyPedJacked`, `PlayJackedTask`, `ms_pendingPedJacks`, `Process`, quién simula qué).
- `client/src/Hooks/TaskHooks.cpp`, `client/src/PacketHandlers/peds.cpp` (`PED_JACKED` ~l.457) y `vehicles.cpp`, `client/src/CNetworkPed*.cpp`/`CNetworkPedManager.cpp` (cómo un ped de red entra/sale de vehículos, `UpdateTaskSync`, `PED_ONFOOT` que lo saca del auto), `server/src/PacketHandlers/peds.cpp` y `vehicles.cpp` (reenvío del paquete).
- Los cambios recientes de traspaso (stages B/D, `git log --oneline`, `client/src/CNetworkPed.cpp` `Release`/takeover) y `CreateGamePed`: ¿le ponen `SetPedDecisionMakerType(-1)` a los NPC de red y por eso no reaccionan?
- Quién es el "syncer" (dueño) del NPC y quién lo simula; si la copia remota del NPC sale del auto por el paquete `PED_ONFOOT` antes/en vez de por la animación, o si el aviso `PED_JACKED` nunca llega/se descarta (¿validaciones de ownership del server? ¿el emisor es el jugador que roba y el receptor el dueño?).

## Entregable
1. Cadena de eventos paso a paso, tal como está hoy en el código (quién manda qué, a quién, qué hace cada cliente).
2. Lista priorizada de causas probables del síntoma (máx. 5), cada una con evidencia `archivo:línea` y cómo confirmarla.
3. Propuesta de arreglo mínima por causa (sin implementarla).
Respondé en el chat con un resumen de ≤10 líneas.
