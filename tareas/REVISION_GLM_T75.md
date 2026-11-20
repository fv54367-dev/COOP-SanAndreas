# Revisión GLM — T75 máquinas fase 3 (commits ae8e3d7 y b46f6d0, SOLO LECTURA)

Revisado por GLM (2026-11-10), rama `tanda/2026-11-10-fable` (wt-fable, HEAD b46f6d0). Sin tocar código.
Alcance: `ae8e3d7` (ZERO4, CMissionWarTable + bit `released`) y `b46f6d0` (HEIST5 torreta). Ambos quedan **SIN REVISAR → revisados**: ningún hallazgo bloquea el merge; M1 (CRLF) conviene arreglarlo antes de instalar.

## Verificado y OK

- **Protocolo**: `shared/config.h` → 0.4.38-alpha. El cambio de formato de `GuestObjectMove` (campo `released` al final de `Serialize`) afecta al server, que **deserializa y reenvía** el paquete (`server/src/PacketHandlers/MissionMachines.cpp` GUEST_OBJECT_MOVE). El handshake compara la versión exacta (`server/src/CNetwork.cpp:332-343`, mismatch → VERSION_MISMATCH), así que el bump es obligatorio y está bien puesto. No hay paquetes nuevos: `ePacketType` y `ePacketType_ToString` intactos y en orden (GUEST_OBJECT_MOVE sigue último). `MachinePhase` kind `RC_GOBLIN`=3 pasa los tres guards: `serialize_int(kind, 0, 4)`, `kind > FORKLIFT` del server y `p.kind > P::FORKLIFT` del cliente.
- **Direcciones contra `_refs\gta-reversed`**: `0x5696C0` FindGroundZFor3DCoord y `0x569E20` TestSphereAgainstWorld (World.cpp:65/71), `0x50C7C0` CCamera::TakeControl (Camera.cpp:96), `0x6D7C90` CVehicle::InflictDamage (Vehicle.cpp:3317), `CPed::m_pAttachedTo` vía `game::Field(me, 0xFC)` (patrón pre-T75, ya probado en ZERO1). Opcodes nuevos verificados contra SB4 (`tools/sb4/data/sa_sbl/classes.db` y `sa_mobile/sa_mobile.json`): **0840 = SET_VEHICLE_AREA_VISIBLE**, **0860 = SET_CHAR_AREA_VISIBLE**; **050E = SORT_OUT_OBJECT_COLLISION_WITH_CAR** confirmado además por el propio script (`Object.SortOutCollisionWithCar`, ZERO4.txt 2837). El resto (0107/0108/0382/0681/0682/04BB/03CB/04E4/015F/0160/04D7/02EB/00A1) son estándar.
- **Hechos del script**: `kTable`/heading 90/área 10 = ZERO4.txt 736-739; imán MINI_MAGNET a -0.2 = 741-743; barriles (BARREL2) attach a -1.2 con rot 90 y tablas (KMB_PLANK) a -1.0 = 2804/2837 (el código separa por modelo, bien); Locate3D de 1 m = 2786; botón Círculo = 2775; `kParked` al lado del BMX del dueño (-1004.097, 1000.789) = ZERO4.txt 727. HEIST5: MINIGUN_BASE `109@` en (2595.5, 2758.0, 22.8) = HEIST5.txt 246 y `Char.AttachToObject` = 863. La maquetación del punto 1 m bajo el toy usa `pos - m_matrix->at`, y `at` es el eje Z del coche en plugin-sdk (up = forward): correcto.
- **RegisterReference**: el código nuevo NO registra referencias (bien). `Wake`/`ApplyToOwned` usan el patrón Remove/Add del T70; `AddToMovingList` es idempotente (gta-reversed Physical.cpp:665: guard `!m_pMovingList`) y `Wake` limpia `m_bIsStaticWaitingForCollision` antes de llamarlo. `Resolve`/`GetPhotoTargets` validan pool + vtable. Las copias de CObjectSync ya usan RegisterReference/CleanUpOldReference por su lado.
- **Pelea con ObjectSync mientras se sostiene**: cubierta por el guard T70 `if (CMissionObjectMoves::IsClaimed(...)) return;` en `CObjectSync::HandleSync` (CObjectSync.cpp:216): la copia enganchada al juguete no la pisa el sync. Las copias heredan `areaCode` del sync (CObjectSync.cpp:276) → visibles en el interior.
- **Interruptor `coop-mission-machines` apagado**: `On()` falso en Machines → `Process` y `OnPhase` no hacen nada (no hay fase, no hay toy); `CMissionWarTable::Process` ve `!On()` → `Leave` restaura (CJ a la calle, `SET_AREA_VISIBLE 0`, imán borrado, `Drop`→`Unclaim`). Comportamiento anterior, como pide la tanda.
- **F1 individual**: `InCoop()` falso → `TakeAway` + reset de fase; el server filtra `m_bIndividualMode` en los tres handlers de Machines; `CMissionObjectMoves::Receive` además filtra `m_bDead`. El CJ vuelve a la calle por WarTable (`GuestToyPending` falso → `Leave`).
- **Desconexión/muerte a mitad**: en la autoridad, mover quieto >1500 ms → `movers.erase` + `Wake` en la última posición (CMissionObjectMoves.cpp:117-125); el claim local se limpia por `told > 1500`. Un invitado muerto no reporta (filtro `m_bDead`), no recibe toy (`PartnerUpdate` corta por health) y `Leave` no teletransporta cadáver (`me->m_fHealth <= 0.0f` → return, respawn se lo lleva).
- **3-4 jugadores**: los spots de toy/torreta se reparten por `GuestRank` (ángulo `rank*4`); el mapa `movers` soporta N objetos en paralelo; `ToyPlace`/`TurretSpot` reintentan con cooldown de 8 s si el piso no está.
- **Compilación**: `xmake.lua` globa `client/src/*.cpp` → `CMissionWarTable.cpp` entra solo. Los símbolos usados existen en el plugin-sdk vendido (`third_party/plugin-sdk`: `SetIsStatic`/`m_bIsStaticWaitingForCollision` en CEntity.h, `AddToMovingList`/`bDisableMoveForce`/`bInfiniteMass` en CPhysical.h).

## Hallazgos

### M1 [Media] — `client/src/CMissionWarTable.cpp` (archivo entero) — CRLF violado
El único `.cpp` nuevo del commit está **entero con LF** (0 de 251 líneas con `\r`); todo lo demás del commit es CRLF (el `.h` nuevo, CMissionMachines.cpp, CMissionObjectMoves.cpp, mission_machines.h). Es la regla dura del repo ("CRLF en .cpp/.h") y la trampa conocida de escribir con la herramienta sin normalizar después. No rompe compilación, pero la próxima edición con herramientas CRLF da diff de archivo entero y el tooling del repo lo asume. Arreglo: normalizar a CRLF con Python antes de mergear.

### M2 [Media] — `client/src/CMissionObjectMoves.cpp:73` — el dueño y el invitado pueden pelear por el mismo objeto
El único arbitraje contra el script del dueño es `if (object->m_pAttachedTo) return false` **en el instante del reporte**. La copia del invitado nunca marca `m_pAttachedTo` (HandleSync la mueve por matriz y la salta mientras está claimed), así que el invitado puede reclamar el mismo barril que el dueño ya lleva con su Goblin: mientras el dueño lo tiene attached los reportes del invitado se ignoran (bien), pero **en cuanto el script del dueño lo suelta, el próximo reporte del invitado (≤100 ms) teletransporta el real a la copia del invitado** y la caída donde el dueño la dejó se pierde. Escenario plausible: dos Goblins cooperando sobre el mismo barril. Sugerencia: cuando `ApplyToOwned` devuelva false por attached, anotar id+timestamp y rechazar los reportes de ese objeto ~2 s (o hasta que el mover venza), para que la suelta del dueño cuente.

### M3 [Media-baja] — `client/src/CMissionObjectMoves.cpp:117-124` — Wake por timeout aunque el invitado siga activo
Si un reporte se atrasa >1500 ms (spike de red) sin que el invitado deje de sostener, la autoridad borra el mover y hace `Wake`: el real cae un instante desde la última posición y cuando vuelve el reporte salta a la copia. Auto-recupera solo, pero se ve el salto. Margen holgado frente al ritmo real de reportes (100 ms), por eso media-baja.

### M4 [Baja] — `client/src/CMissionObjectMoves.cpp:209-215` — el `released` de un segundo invitado se descarta
Con dos invitados sobre el mismo objeto: B reclama localmente (los `claims` son por cliente), se le adjunta la copia, pero la autoridad le ignora los reportes mientras el mover de A siga vivo. Si B suelta en esa ventana, su paquete `released` entra en `Receive` y **la regla de takeover lo descarta antes del `movers.erase`** → el objeto queda en la última posición de A y despierta por timeout. Lo que hizo B se pierde sin aviso (consistente con "un dueño por objeto", pero confuso en juego: B levantó y soltó y no pasó nada).

### M5 [Baja] — `client/src/CMissionWarTable.cpp:69,87` — `s.magnetObj` es un pool ref sin revalidar identidad
`TakeDownMagnet` y el guard de `HangMagnet` confían en `CPools::GetObject(s.magnetObj)`: si el objeto fue borrado y el slot reusado por otro objeto, borra/ignora el equivocado. Probabilidad muy baja (solo nosotros creamos ese objeto en el interior del invitado) y el patrón previo (`own.base`) ni siquiera valida. Informativo.

### M6 [Baja] — `client/src/CMissionMachines.cpp:41` — `kParked` compartido entre invitados
Con 3-4 jugadores, el CJ congelado de todos los invitados espera en el mismo punto (-1006.5, 1000.8): superpuestos visualmente (congelados no se empujan). Los juguetes sí se reparten por rank. Cosmético; sa-coop tenía un solo invitado.

### M7 [Baja/probar] — `client/src/CMissionMachines.cpp:288` — `below=7` acepta el piso de la sala
`FindGroundZ` cerca de `kTable` encuentra la maqueta **o el piso del cuarto** (1344.4, dentro del rango aceptado): el Goblin del invitado puede aparecer en el piso en vez de sobre la mesa, y el `TestSphereAgainstWorld` puede rechazar los puntos sobre la maqueta. Vuela igual, pero hay que verlo en juego (la tanda ya lo pide como prueba simple).

### M8 [Info/docs] — `shared/network/packets/mission_machines.h:16` — comentario desactualizado
`RC_GOBLIN` dice "defined for later work: not emitted yet" y T75 ya lo emite. Menor: `kAgainMs=500` (CMissionWarTable.cpp:24) vs el cooldown de ~1000 ms del script (103@ > 1000, ZERO4.txt 2774-2776) — desviación inofensiva.

## Resumen
- 0 bloqueantes; 1 de proceso (M1 CRLF), 2 de lógica que conviene anotar para una tanda de arreglos (M2 pelea dueño/invitado, M3 lag), y casos borde/3-4 jugadores bien resueltos con los timeouts existentes.
- Protocolo, versión y orden de paquetes: correctos. Direcciones y opcodes verificados contra gta-reversed/SB4 y contra los propios ZERO4/HEIST5.txt.
