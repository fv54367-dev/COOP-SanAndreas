# Informe de auditoría de sincronización (2026-11-10, solo lectura)

> Hecho por GLM/ZCode a pedido del dueño, en paralelo con T48. **No se cambió código.**
> Los hallazgos son PROPUESTAS: nada de esto está aprobado ni planificado todavía.

**Alcance leído:** `CNetworkPed*`, `CNetworkPedManager`, `CNetworkVehicle*`(+Manager), `CNetworkPlayer*`(+Manager), `CPedDamageSync`, `CTaskSequenceSync`, `CNetworkAnimQueue`, `CAimSync`, `CJackSync`, `CStreamingSync`, `COpCodeSync`, `CMissionMirror` (parcial), `CServerTime`, `CNetworkThread`, `Main.cpp` (bucle de sync), handlers `PacketHandlers/{peds,vehicles,players,scripts}.cpp`, hooks `PedHooks/PlayerHooks`, lado server (`PacketHandlers/peds.cpp`, `CHandover.cpp`), paquetes en `shared\network\packets\peds.h`, y docs (`ESTADO.md`, `docs\SISTEMAS.md`, tandas 2026-11-09/11-10, AUDIT_JACK).

**Contexto general:** el código está extraordinariamente bien auditado y comentado; casi todo lo "raro" que se encontró resultó tener un comentario que lo explica o estar en `ESTADO.md` como conocido/aceptado. Los handlers de red corren todos en el hilo del juego (`CNetwork::ProcessReceive` en `gameProcessEvent.before`, `Main.cpp:111`), con cola mutex en `CNetworkThread`, así que no hay races de hilos en la aplicación de paquetes. No se encontró ningún bug crítico nuevo.

## Hallazgos (priorizados)

### 1. NPC pasajero: `PedPassengerSync` sin el guard de `seatid = -1` que sí tiene la versión de players — reclama el asiento delantero
- **Archivos:** `client\src\CNetworkPedManager.cpp:165-185`, `shared\network\packets\peds.h:353` (`int8_t seatid;` sin inicializador, se serializa rango -1..7), receptor en `client\src\PacketHandlers\peds.cpp:459-469`.
- **Evidencia:** en `Update()`, el paquete se manda con `packet{}` (seatid queda en 0) y el bucle que busca al ped en `m_apPassengers[0..m_nMaxPassengers-1]` solo asigna `seatid` si lo encuentra; si no lo encuentra (transición de asiento durante un shuffle, ped con `bInVehicle` pero fuera del array un instante), se envía `seatid = 0`. El receptor (`PED_PASSENGER_UPDATE`) hace `inRightSeat` falso → `WarpIntoVehiclePassenger(vehicle, 0)`. Los autores conocen exactamente esta clase de bug: el paquete análogo de players (`VehiclePassengerUpdate`) inicializa `seatid = -1` con el comentario "*sending the default 0 would claim the front passenger seat...*" y no envía hasta encontrar el asiento (`PacketHandlers\vehicles.cpp:398-413`). Además el handler NPC no valida `IsValidPassengerSeat` (la versión de players sí, `vehicles.cpp:614`).
- **Gravedad:** media. **Síntoma:** un NPC pasajero remoto aparece teletransportado al asiento delantero (o compite con quien lo ocupe) en los demás clientes; warps espurios intermitentes durante cambios de asiento.
- **¿Conocido?** No aparece en `tareas\` ni en los informes de auditoría (la AUDIT_JACK habla del seatid faltante de `PED_JACKED`, ya arreglado).

### 2. Typo en `CAimSync::GetCameraResyncRate`: mezcla `Old.x` con `New.y` — envío de cámara a máxima tasa moviendo el mouse en un solo eje
- **Archivo:** `client\src\CAimSync.cpp:219-220`.
- **Evidencia:** la detección de "just stopped moving the mouse" es `(New.x == 0 || New.y == 0) && (Old.x != 0 || New.y != 0)`. El segundo término compara la X vieja con la Y nueva; la condición correcta (según el comentario y la lógica) es `New.x==0 && New.y==0 && (Old.x!=0 || Old.y!=0)`. Con movimiento puramente horizontal (Y==0, X activa, Old.x!=0) la rama se cumple **todos los frames**: sin apuntar pone `nResyncRate = 0` (envía `PLAYER_CAMERA_SYNC` en cada frame en que el estado cambie, ~30-60/s en vez de 66 ms) y apuntando usa 50 ms en vez de 100.
- **Gravedad:** baja-media. **Síntoma:** burst de tráfico de cámara + sobresync cuando alguien gira la cámara en un eje (típico en aiming con mouse); en 3 jugadores suma. Sin crash.
- **¿Conocido?** No documentado.

### 3. Reintento de opcodes de animación sin timeout: `LoadAllRequestedModels` cada frame para siempre si el bloque IFP nunca carga
- **Archivos:** `client\src\COpCodeSync.cpp:1477-1496` + `CNetworkAnimQueue.cpp:14-39`.
- **Evidencia:** cuando un `TASK_PLAY_ANIM*` llega con el bloque sin cargar, `HandlePacket` hace `RequestModel + LoadAllRequestedModels(false)` y re-encola el opcode; `CNetworkAnimQueue::Process()` corre **cada frame** (`Main.cpp:137`), así que un bloque que nunca termine de cargar (nombre corrupto de un IFP moddeado, disco lento) produce una llamada a `LoadAllRequestedModels` por frame indefinidamente. El cap de 256 entradas limita la memoria pero no el costo por frame (siempre es 1 entrada reintentándose). Compárese con los estados pending de `CNetworkPed` (npc-named-anim-sync) que sí tienen timeout de 5 s.
- **Gravedad:** baja. **Síntoma:** microstutter persistente local desde que llega una anim de bloque inexistente (raro con assets vanilla).
- **¿Conocido?** No documentado.

### 4. Trailer en `VEHICLE_DRIVER_UPDATE`: se procesa antes de validar el vehículo y rompe el tow-link con `trailerid` inválido
- **Archivo:** `client\src\PacketHandlers\vehicles.cpp:263-275` (las validaciones del vehículo están recién en las líneas 277-286).
- **Evidencia:** el bloque del trailer usa `pNetworkVehicle->m_pVehicle` sin `IsVTableValid()` (todos los demás usos del handler validan primero). Y si el conductor arrastra un trailer que **no** es vehículo de red en su cliente (p. ej. vehículo de misión local-only), `trailerid` viaja inválido → `GetVehicle` devuelve nullptr → si acá el tractor tenía remolque enganchado, se llama `BreakTowLink()` en cada update hasta que el remolque de red exista acá (se auto-cura, pero engancha/desengancha visiblemente).
- **Gravedad:** baja. **Síntoma:** remolque que se desengancha/se reengancha de a ratos cuando el dueño no lo registra como vehículo de red.
- **¿Conocido?** No documentado.

### 5. `UpdateTaskSync` no detecta el "reinicio" de una misma animación (ni flags/tiempo)
- **Archivo:** `client\src\CNetworkPed.cpp:1388-1391` (el `changed` compara grupo/id/hash pero no `animFlags`/`animTime`) y `CNetworkPed.cpp:1004-1006/1156` (el receptor, si la asociación sigue viva y es la misma, la deja "keep it playing" sin reiniciar el tiempo).
- **Evidencia:** si el syncer vuelve a reproducir la misma anim one-shot (teléfono, `getup`, baile reiniciado), el estado no cambia en el emisor (salvo el resend de 3 s solo si hay un jugador a ≤60 m) y el receptor tampoco la reinicia. La copia se pierde el replay o lo ve hasta 3 s tarde.
- **Gravedad:** baja. **Síntoma:** animaciones one-shot de NPC que "no se vuelven a ver" en copias lejanas de jugadores.
- **¿Conocido?** Emparentado con el bug 3 de la tanda npc-named-anim-sync (que cubría el stream-in, no el replay); este lado emisor/receptor no está anotado.

### 6. `PED_PASSENGER_UPDATE` acepta `seatid` fuera de rango del modelo
- **Archivo:** `client\src\PacketHandlers\peds.cpp:459-469` vs el guard equivalente en `vehicles.cpp:614` (`IsValidPassengerSeat`).
- **Evidencia:** el paquete viaja con rango -1..7 pero el modelo puede tener menos asientos; `WarpIntoVehiclePassenger` → `ComputeTargetDoorToEnterAsPassenger(vehicle, seatid)` con asiento inexistente. El propio comentario del código de players advierte "*the game doesn't check it before warping (it writes out of m_apPassengers / uses an invalid door)*". Hoy solo lo dispara un paquete corrupto/cruzado entre versiones (el emisor real clampa a asientos válidos), por eso baja.
- **Gravedad:** baja (defensa en profundidad).
- **¿Conocido?** No.

## Cosas que parecen sospechosas pero resultaron estar bien (no re-investigar)

- **Vida de NPC en byte (máx 255), velocidad de `PedOnFoot` comentada en el Serialize (copias "a saltos"), copia que tarda 1-2 s en caer, ocupantes muertos no rearmados, interruptores no negociados al conectar** — todo ya en `ESTADO.md` (bug 7 y notas de T46/T47) o aceptado por el dueño.
- **Tasks armados en la pila (`CTaskSimpleCarSetPedIn*`)** — el crash de `ResolveReferences` ya está mitigado con `CUtil::ReleaseStackCarTask` en los 8 sitios (SISTEMAS.md).
- **`param.value = 0` para todo parámetro PLAYER en `COpCodeSync::HandlePacket`** — intencional: cada cliente aplica el comando a su propio jugador (set_player_control, wanted, etc.).
- **SetTowLink del trailer (`vehicles.cpp:273`)** — la llamada `pTrailer->SetTowLink(pTractor, true)` coincide con la firma virtual de gta-reversed (`Trailer.cpp:140`, es método del remolque).
- **Heurística de aim `vecTarget.x==0 || y==0` en `CNetworkPedManager::Update:216`** — sustituye la posición solo si la coordenada es exactamente 0 (medida nula en el mundo); no vale la pena tocarla.
- **`ResolveAmbientVehicleOverlap`** — la regla "gana el id menor" es simétrica en ambos clientes y devuelve inmediatamente tras borrar; sin doble-borrado.
- **Serial de tarea uint8 con wrap** — el drop `serialDelta <= 0 && > -32` en `ApplyTaskState` maneja duplicados, reorder y wrap, y acepta saltos grandes como "syncer nuevo".
- **Asignación/desasignación de syncer (server `CHandover.cpp` + cliente `ASSIGN_PED`)** — el unassign al viejo syncer existe (fue el fix de los 65.000 avisos), el guard de estado explícito evita flips por paquetes repetidos, y el canal EVENT es confiable/ordenado.
- **`KillCopy`/`ProcessCopy` (rearmado de copia muerta)** — deliberado y logueado (`ped-copy-revive`); el "handle nuevo del pool" está anotado como deuda en la tanda.
- **`CPlayerPed__ProcessControl_Hook` pisando health/armour del player remoto cada frame** — es el modelo de autoridad (cada cliente es dueño de su vida); `ComputeWillKillPed_Hook` anula muertes espurias coherente con eso.
- **Mezcla de dominios de tiempo (`GetTickCount` vs `GetSyncTime` vs `serverTime` en la interpolación de cámara)** — cada subsistema usa uno solo consistente; la interpolación de cámara divide elapsed-local por delta-server, aproximación válida con clamp 0..1.
- **`PED_SAY` y `GetEntity()` sin `IsVTableValid`** — los punteros vienen de referencias registradas (`RegisterReference`) de los managers, se mantienen válidos.
- **Re-encolado de secuencias de tareas fallidas (`CTaskSequenceSync::HandlePacket`)** — cierra/limpia la secuencia abierta antes de re-encolar; el cap de 256 del `CNetworkAnimQueue` cubre crecimiento.

**Balance:** el sistema de daños con autoridad única (T46), el handover de peds y el jack-sync son las zonas más sólidas; la deuda real se concentra en asimetrías jugador-vs-NPC (hallazgos 1 y 6) y en costos per-frame sin timeout (2 y 3).
