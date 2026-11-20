# TANDA 2026-11-10 — GLM (ZCode): sync de trenes, núcleo espejado

> **Quién:** GLM (ZCode CLI), a pedido directo del dueño (mismo camino que T48).
> **Alcance aprobado por el dueño (2026-11-10):** núcleo espejado + solo el host genera trenes random.
> **Diseño de referencia:** `_refs\project_gta_sa_coop\docs\design\trains.md` (GPL-3.0, decisiones T-D1..T-D9
> confirmadas 2026-10-18). Portamos su Opción A ("track mirror con identidad de vagones"), adaptado a este repo.
> Cita GPL en el commit. **Cambio de protocolo: `COOPANDREAS_VERSION` 0.4.22-alpha → 0.4.23-alpha** (T49-T51 tomó el 0.4.22 y dejó el 0.4.23 anotado para esta tanda en su commit) (único dueño
> del protocolo en esta tanda; recompilar `testbot`).

## Objetivo
Que todos los jugadores vean LOS MISMOS trenes (random y de misión) en el mismo lugar, moviéndose igual, y que se
puedan subir adentro. Hoy: `CreateVehicle` devuelve `false` para `VEHICLE_TRAIN` (`client/src/CNetworkVehicle.cpp:82`),
cada juego genera sus propios randoms (el handshake revierte `DisableRandomTrains`) y un jugador en tren se ve
congelado donde subió en la otra pantalla.

Cómo: el **host** simula todos los trenes; los demás crean una copia real con `CTrain::CreateMissionTrain` del propio
juego (misma config/vía/nodo) y la mueven por **distancia en riel** (`TRAIN_STATE` 5 Hz). Cada vagón además es un
`CNetworkVehicle` solo para identidad (asientos, blips, disparos por los caminos existentes). Randoms **solo del host**
(wrapper en 0x43426D). Desraílado → la copia se borra en los receptores. Fuera de alcance (tandas futuras): espejo de
attach (peds arriba del tren, T2), botón "próxima estación" (T4), manejo del invitado pulido (T5), desraíle con física.

## Archivos a tocar (lista exacta)
Nuevos:
- `shared/network/packets/train.h` — `TrainState` (canal SYNC), `TrainEvent` (canal EVENT: SPAWN/REMOVE/CARRIAGES/SNAPSHOT/REQUEST), helper `IsTrainModel()`.
- `server/src/PacketHandlers/train.cpp` — relay del host a los demás (state/event), SNAPSHOT a uno, REQUEST solo a la autoridad, validación `IsScriptAuthority`.
- `client/src/CTrainSync.h` / `client/src/CTrainSync.cpp` — módulo: host scan + state 5 Hz + eventos + snapshot; follower: copia + bind + rebuild; override de copia en el ProcessControl del CTrain; wrapper 0x43426D.

Editados:
- `shared/config.h` (versión 0.4.22-alpha), `shared/network/packet_types.h` (TRAIN_STATE, TRAIN_EVENT al final + strings).
- `server/src/PacketHandlers/vehicles.cpp` (vagón: sin ReassignSyncer ni SyncToOccupant), `server/src/CHandover.cpp` (vagones nunca se traspasan; al dejar el host se remueven).
- `client/src/CNetworkVehicle.cpp` (`CreateVehicle` devuelve false pero el registro QUEDA para trenes: distinción "sin vehículo" de "modelo inválido"), `client/src/PacketHandlers/vehicles.cpp` (spawn conserva registro de tren; idle/driver no mueven vagón; remove de vagón borra la cadena; assign solo flag), `client/src/CNetworkVehicleManager.cpp` (host no manda idle de vagón), `client/src/CStreamingSync.cpp` (registros de tren fuera del streaming), `client/src/CMirrorBlips.cpp` (excepción de trenes condicionada al interruptor), `client/src/PacketHandlers/system.cpp` (snapshot en PLAYER_CONNECTED; OnBecameFollower en PLAYER_ASSIGN_HOST), `client/src/Main.cpp` (CTrainSync::Process en el tick), `client/src/Hooks/VehicleHooks.cpp` (override de copia dentro de `CVehicle__ProcessControl_Hook` + redirect 0x43426D), `client/src/CConfigLoader.h/.cpp` (interruptor `train-sync`).

## Restricciones
- CRLF en `.cpp/.h` (editar con Python, `open(..., newline='')`; nunca `sed -i`). `tareas/*.md` van en LF.
- Comentarios en inglés, cortos; estilo del código vecino (camelCase, `m_`, `CXxx`).
- No tocar la lista `CWorld__Add_Addresses` (el `CWorld::Add` de `CreateMissionTrain` en 0x6F784B queda FUERA a propósito: los vagones se networkan por el scan del host, y una copia jamás se networka por sí misma).
- No reordenar `ePacketType` existente (static_asserts). Paquetes nuevos al final.
- Si cambia el protocolo: bump de versión + testbot recompilado (hecho en esta tanda).
- `_refs\` solo lectura; `_refs\gta-reversed` como referencia de semántica (citar).

## Decisiones que toma esta tanda
- T-D2: randoms solo del host (ambos ven los mismos; invitado solo en otra ciudad no ve randoms ahí).
- T-D3: un tren random que un invitado lleva lejos del host se conserva (flag de misión temporal durante la llamada del generador).
- T-D4/T-D9: el tren lo simula siempre el host; si el host se va, los trenes desaparecen (el server remueve sus vehículos) y el nuevo host genera los suyos.
- Desraíle: la copia se borra en los receptores (jugadores afuera primero) hasta la tanda de física (T5).
- El driver NPC cosmético que `CreateMissionTrain` le pone a la copia (no es tram): se lo saca cuando el state dice que el motor del host no tiene NPC driver, o cuando un jugador real de red queda sentado ahí (warp existente).

## Casos borde
- **3-4 jugadores:** todos copian del mismo host; `SNAPSHOT` individual a cada recién conectado; `REQUEST` solo al host.
- **Host desconectado:** server remueve sus vehículos → `VEHICLE_REMOVE` borra cadenas (jugadores afuera); server asigna nuevo host → el wrapper le vuelve a permitir generar y su scan empieza a sync.
- **Invitado en individual (F1) o sin autenticar:** gate cerrado, wrapper corre el original → juego de a uno sin cambios.
- **Interior / cine:** el generador ya no corre en interiores (juego original); las copias siguen y se congelan a los 500 ms sin state (host alt-tab / pausa).
- **Invitado juega FREIGHT local:** su `DELETE_ALL_TRAINS` borra copias → rebuild desde el descriptor en el próximo state (riesgo #7 del diseño, aceptado).
- **Interruptor apagado (`train-sync=false`):** sin paquetes TRAIN_*, registros de tren descartados como hoy, blips de tren por posición, cada juego genera sus randoms (todo exactamente como hoy).
- **Vagón con jugadores adentro al borrar la copia:** `RemoveOccupants` (`CNetworkVehicle.cpp:126`) los deja al lado del vagón.

## Crítica de Codex a la tanda (2026-11-10, leída y evaluada)
Puntos incorporados al diseño:
1. Orden entre canales: TRAIN_STATE (SYNC) puede llegar antes que el SPAWN/los VEHICLE_SPAWN (EVENT). El receptor
   cachea el último state por trainid hasta que llega el SPAWN; los states se descartan por número de secuencia
   viejo; SPAWN repetido para un trainid ya armado solo re-bindea si el descriptor cambió; REMOVE de id desconocido
   se ignora.
2. `ResolveAmbientVehicleOverlap` y el ghosting solo miran created-by RANDOM/PARKED/MISSION: los vagones son
   PERMANENT (4), pasan sin tocar (verificado, no requiere cambio).
3. Driver sync de vagón: se salta SOLO la transformación (pos/rot/velocidad/health/colores); arbitraje de asientos,
   warp del jugador, arma/vida/keys y radio siguen. El server sigue asentando al ocupante y su posición.
4. Sin idle updates de vagones, el server queda con posición vieja: es inofensivo porque los vagones nunca son
   candidatos de handover (excluidos por modelo).
5. Release()/RemoveHostedUnused(): un vagón nunca toma el camino de `Release` (su created-by no es ambiente), y
   `RemoveHostedUnused` borra el registro de un vagón del host cuyo vehicle murió mandando VEHICLE_REMOVE (lo hace
   el dtor): la cadena se entera por ahí; el scan del host también detecta la cabeza perdida.
6. El override de la copia va ANTES de la rama del jugador local y de `ProcessPositionError` en
   `CVehicle__ProcessControl_Hook`: si el invitado se sube a manejar la copia, su juego no integra nada (status
   forzado a 4) y el tren sigue lo que dice el host (T-D4).
7. El override conserva TODO el resto del ProcessControl original: posición/matriz/RW/sectores/colisiones los
   coloca el propio juego desde la distancia en riel fijada (solo se clavan distancia, velocidad y status).
8. Borrado/rebuild idempotente: REMOVE repetido es no-op; cadena borrada invalida los registros de todos los
   vagones; pérdida local (vehicle null, descriptor vivo) reconstruye en el próximo state.
9. Interruptores mixtos host/invitado: quien no manda no afecta al otro; el invitado con `train-sync=false`
   descarta registros de tren (comportamiento de hoy). Al perder la autoridad se borran las copias antes de que
   el wrapper vuelva a dejar generar (sin duplicados).
10. Autoridad de trenes = `IsScriptAuthority` (el host de la historia). Los trenes de una misión individual de un
    invitado son locales: nadie los escanea (el scan corre solo en el host) y el server rechazaría sus TRAIN_*.

## Criterio de aceptación
- `xmake build client` / `server` / `testbot` compilan; testbot TODO PASS.
- Sin cambios de comportamiento con `train-sync=false` (revisar diff: todo lo nuevo gated por el interruptor o por "hay registro de tren").
- `git diff --check` limpio; CRLF intacto en los editados.

## Resumen (≤10 líneas)
Sync de trenes espejando el diseño del repo hermano: host simula, receptores crean copias reales por `CreateMissionTrain` y las mueven por distancia en riel a 5 Hz; cada vagón es registro de vehículo para asientos/blips/disparos; randoms solo del host (wrapper 0x43426D); desraíle borra la copia; protocolo 0.4.22-alpha (TRAIN_STATE/TRAIN_EVENT); interruptor `train-sync`. Sin tocar scripts ni MAIN. Pendiente: revisión de diff y prueba en juego (TESTEAR_HOY.md).

## Coordinación (nota de Claude, 2026-11-10 04:00)
Estamos los dos en el mismo árbol y los dos tocamos protocolo. Claude ya commiteó **0.4.22-alpha = `PED_ANIM_SET`** (T49, `TANDA_2026-11-10_sync_visual.md`), al final de `packet_types.h`.
- Trenes: usar **0.4.23-alpha** y agregar `TRAIN_STATE` / `TRAIN_EVENT` **después** de `PED_ANIM_SET` (enum y lista de nombres).
- Archivos que tocamos los dos: `shared/config.h`, `shared/network/packet_types.h`, `client/src/Main.cpp`, `client/src/CConfigLoader.h/.cpp`. Claude ya terminó con ellos: partir de `HEAD`, no de una copia anterior.
- Claude no toca `CNetworkVehicle*`, `VehicleHooks.cpp`, `CStreamingSync.cpp`, `CMirrorBlips.cpp`, `system.cpp`, `vehicles.cpp` ni `CHandover.cpp`.

## Revisión 2026-11-10
- T62 verificó los ocho hallazgos; ninguno resultó falso. Tabla 0x8D44F8: pefile sobre exe 1.0 US confirma uint32 (primera fila 537, 569, 569, 569, 569, 0), no uint16.
- Copia perdida/descarrilada: destruir el vehículo conservando los registros de identidad; REMOVE definitivo libera también los registros, incluso sin copia viva.
- Autoridad única = host de conexión (`m_bIsHost`) en cliente y server, independiente del dueño de la misión coop. Esta decisión reemplaza la mención anterior a IsScriptAuthority.
- `OnBecameFollower` respeta train-sync apagado; blips por posición apagado / identidad encendido.
- Antes de liberar vagones se llama a CWorld::RemoveReferencesToDeletedObject; ocupantes salen por DestroyGameVehicle. Referencias: gta-reversed Train.cpp RemoveOneMissionTrain (0x6F5DC0), NumTrackNodes/pTrackNodes y sa-coop-karimov Trains.cpp (MIT, consultado; llamada a API nativa, sin copiar implementación).
- Snapshot inicializa posición pero no una secuencia ficticia: el primer TRAIN_STATE se acepta siempre, incluso seq 128..255; siguientes mantienen orden modular.
- Descriptor validado: config 0..15, count/modelos exactos de configuración, ids válidos y distintos, vía cargada, nodo válido y distancia finita en el circuito. States inválidos o de otra vía se descartan.
- Protocolo final: 0.4.23-alpha; sin cambio adicional del formato de paquetes.
- Fase siguiente T5: el invitado en cabina todavía NO puede conducir; referencia `TRAIN_DRIVE`/`TRAIN_ENTER` de sa-coop-karimov. No implementado en T62.
- Validación: client recompilado completo (-r); server/testbot y prueba automática se registran en la tanda T62. Sin instalar ni probar en juego; revisión cruzada de Claude pendiente.
