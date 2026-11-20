# TANDA 2026-11-18 — objetos de interior + pizzero atendiendo al invitado (Hermes/Qwen4-Max)

Rama: `tanda/2026-11-18-objetos-interior` (desde main con el fix de comensales `8caa22d` ya integrado).
Pedido del usuario: que el invitado vea (1) la pizza/burger que comen los comensales y la bandeja/caja
del pizzero, y (2) la animación del pizzero atendiendo cuando el que compra es el invitado — que la vean todos.

## Contexto (evidencia)

- Los comensales (script ambiental `PCHAIR`) crean `CJ_PIZZA_2`/`CJ_BURG_2` con `CREATE_OBJECT` y los
  toman con `TASK_PICK_UP_OBJECT` (0x070A) en el hueso `PED_HANDR` (`HOLD_ORIENTATE_BONE_FULL`).
  El pizzero (`junkfud.sc`, servicio JFUD) crea la bandeja/caja (`GET_ITEM_IN_SHOP` → `CREATE_OBJECT`)
  y la toma igual; sus poses `SHP_Tray_*` van por 0x0812 (parcial sin task, como los comensales).
- `CObjectSync` solo publica objetos `OBJECT_MISSION` durante misiones sincronizadas con autoridad de
  script: en libre por la pizzería no se envía nada de esto.
- Cuando el INVITADO compra, su `junkfud` local anima a su pizzero local, que está OCULTO detrás de la
  copia del dueño (`CInteriorOwnership::IsServicePed` + `s_hiddenPeds`): el invitado ve al pizzero
  visible quieto. El task de hold del juego (`CTaskSimpleHoldEntity`) mueve el objeto con el hueso cada
  frame (`SetPedPosition`, `RpHAnimHierarchyGetMatrixArray`) — es el mecanismo nativo a reutilizar.
- El server ya decide el dueño de interior por menor `m_nPopulationOrder` en la misma sala
  (`PacketHandlers/peds.cpp`, INTERIOR_POPULATION) y ya reenvía paquetes invitado→dueño con validación
  (`MissionGuestActions.cpp`, kinds INPUT/THROW/READY). Ambos patrones se reutilizan.

## Diseño

### Parte 1 — `interior-object-sync` (dueño → invitados)

- Paquete nuevo `INTERIOR_HELD_OBJECT` (`Packets::Peds::InteriorHeldObject`, canal STATE como los de
  peds): `objectid` (uint32, espacio propio), `pedId` (ped de red que lo sostiene), `modelId`
  (uint16, 0 = quitar), `bone` (uint8), `offset` (3 floats), `area` (uint8).
- Dueño: `CInteriorObjectSync::Process()` nuevo (llamado junto a `CObjectSync::Process()` en
  `Main.cpp`), cada 100 ms: con el interruptor prendido y siendo dueño de interior, para cada ped
  propio con `m_bInteriorPopulation` (incluye servicios adoptados) buscar
  `FindActiveTaskByType(TASK_SIMPLE_HOLD_ENTITY)` (busca primarias y secundarias, verificado en
  gta-reversed) → `m_pEntityToHold`. Publicar al aparecer/cambiar (modelo, hueso, offset > 0.02)/
  desaparecer; reenvío periódico cada 3 s si hay otro jugador cerca (stream-in tardío).
- Invitado: copia visual `CObject` (`OBJECT_MISSION` para que el juego no la borre; el hold task le
  pone `collision=false` y `bAttachedToEntity` solo, verificado en `TaskSimpleHoldEntity.cpp`).
  Adjuntar con `CTaskSimpleHoldEntity` propio del juego en slot secundario
  `TASK_SECONDARY_PARTIAL_ANIM`, `animId=ANIM_ID_NO_ANIMATION_SET` (no reproduce anim),
  `bDisAllowDroppingOnAnimEnd=true`. El objeto sigue al hueso del ped copia con la pose YA
  sincronizada, sin paquetes por frame. Si el ped copia no está streameado: guardar pendiente y
  aplicar al entrar (como el task state). Auto-curado: si el hold task desaparece (abort por evento)
  y el objeto sigue activo, reponerlo. `modelId=0`/remove → sacar task y destruir copia.
- Cambio de dueño de interior: el invitado limpia todos los objetos de interior al cambiar el dueño
  (evita colisión de ids entre espacios) y el dueño nuevo reenvía todo (`ResendAll` al hacerse dueño).
- Interruptor nuevo `interior-object-sync` (default true; apagado = comportamiento anterior).

### Parte 2 — `interior-service-guest` (invitado → dueño → todos)

- Paquete nuevo `INTERIOR_SERVICE_POSE` (`Packets::Peds::InteriorServicePose`, canal EVENT):
  `senderPlayerid` (lo pone el server), `owner`, `pedId` (el pizzero visible, el invitado lo conoce
  por su copia remota), `area`, `kind` (UPDATE/CLEAR), `animBlock[16]`, `animHash`, `animFlags`,
  `animTime`, `animSpeed`, `heldModel` (uint16, 0 = nada), `bone`, `offset`.
- Invitado: con el interruptor prendido y siendo invitado de interior, cada 150 ms buscar el ped de
  servicio local OCULTO (`IsServicePed` + en `s_hiddenPeds`) con pose nombrada (reutilizar
  `FindNamedAssociation`, hoy static: exponer helper) o hold task; enviar UPDATE con pose+objeto al
  dueño actual (`CInteriorOwnership`), apuntando al `pedId` de la copia remota que lo representa
  (match modelo+posición < 4 m, como `IsRepresentedRemotely`). Al desaparecer pose y objeto → CLEAR.
- Server: validar que el sender está en un interior (`m_nPopulationArea != 0`), que `owner` es
  realmente el dueño de esa sala (menor order, misma regla que INTERIOR_POPULATION) y distinto del
  sender; poner `senderPlayerid`; reenviar SOLO al dueño.
- Dueño: resolver `pedId` → ped propio de servicio de mi interior. Aplicar la pose nombrada como
  asociación parcial (mismo camino que `ApplyNamedAnimState`: pedir bloque si falta, blend con flags
  menos translate, time/speed) y crear el objeto sostenido + hold task (reutiliza el mecanismo de la
  parte 1). Guardar override por ped (anim propia, objeto, sender, lastSeen).
  **Prioridad del script nativo**: si el ped real tiene OTRA asociación parcial nombrada con
  blend > 0.5 (el dueño está comprando), el override cede (no se refresca; se retira nuestra anim);
  si lo nativo se va y el override sigue fresco, puede volver. **Timeout**: sin UPDATE por 1.5 s →
  CLEAR (saca pose y objeto). La sync normal del dueño (pose por `UpdateTaskSync` + objeto por parte 1)
  lo propaga a TODOS, incluido el comprador.
- Dos invitados comprando a la vez: gana el primer sender; el segundo se ignora mientras haya override
  activo (se loguea). Su animación local sigue oculta; ve al pizzero atendiendo al primero.
- Interruptor nuevo `interior-service-guest` (default true; la pose funciona aun con
  `interior-object-sync` apagado, la bandeja no).

### Protocolo

- `COOPSA_VERSION` → `0.4.60-alpha` (`shared/config.h`). Paquetes nuevos AL FINAL de
  `COOP_PACKET_TYPES` (`packet_types.h`; los nombres de `ePacketType_ToString` salen de la macro,
  mismo orden). Recompilar cliente, server y testbot; correr testbot local → TODO PASS.

## Archivos a tocar

- `shared/config.h`, `shared/network/packet_types.h`, `shared/network/packets/peds.h`
- `client/src/CInteriorObjectSync.{h,cpp}` (nuevo), `client/src/CNetworkPed.{h,cpp}` (helper de pose
  nombrada + aplicar override en el dueño), `client/src/CInteriorOwnership.{h,cpp}` (consultas de
  rol/owner/servicios ocultos), `client/src/CConfigLoader.{h,cpp}`, `client/src/Main.cpp`,
  `client/src/PacketHandlers/peds.cpp`
- `server/src/PacketHandlers/peds.cpp` (routing INTERIOR_SERVICE_POSE)
- `INTERRUPTORES.md`, `docs/SISTEMAS.md`

## Restricciones

- Nada de protocolo fuera de los paquetes nuevos al final; versión una sola vez en esta tanda.
- CRLF preservado; estilo del código vecino; comentarios en inglés cortos.
- Preferir funciones del juego (hold task, blend de anims) antes que simular a mano.
- No tocar `scm/` ni las reglas de misión: todo en C++.
- main no se toca hasta OK del usuario.

## Criterio de aceptación (prueba del usuario, 2 GTAs)

1. En la pizzería, el invitado ve la pizza/burger en la mano de los comensales, siguiendo la
   animación de comer (sin tembleque ni desfase grande).
2. El invitado ve la bandeja/caja en la mano del pizzero.
3. El INVITADO compra: todos ven al pizzero hacer la secuencia de atender (bandeja incluida).
4. Al soltar/huir (pánico), el objeto desaparece en todos; al salir del interior no quedan copias.
5. Interruptores apagados → comportamiento anterior exacto.
6. Testbot TODO PASS; cliente y server compilan; sin crashes en sesión de 2 jugadores.

## Casos borde

- 3–4 jugadores: todos los invitados reciben lo mismo (el dueño publica una vez).
- F1 individual a mitad de compra: el sistema de interiores no depende del modo misión; sigue.
- Invitado se desconecta a mitad de compra: timeout de 1.5 s limpia el override en el dueño.
- Dueño sale del interior: handover existente; invitados limpian objetos al cambiar dueño; el nuevo
  dueño reenvía; overrides de servicio mueren por timeout.
- Dueño compra a la vez que un invitado: gana el script nativo del dueño (override cede).
- Dos invitados comprando: primero gana, segundo ignorado (logueado).
- Modelo no streameado en el invitado: pedir + pendiente + reintento (como anim blocks).
- Invitado streamea tarde al ped: resend periódico de 3 s + aplicar pendiente al entrar.
- Pánico de pizzería: el comensal suelta el objeto (DROP_OBJECT) → hold task se va → remove.
- Guardado/carga: objetos de interior son transitorios; Reset los limpia (no van al save).
- Interruptor apagado a mitad de sesión: Process deja de publicar; al siguiente cambio de dueño o
  Reset se limpian copias (documentar que apagado en caliente puede dejar copias hasta salir).

## Ajustes por crítica de solo lectura (18-11, subagente; verdict proceed-with-changes)

1. Canal: no existe STATE; ambos paquetes van por EVENT (confiable), como ObjectSync/PedTaskState.
2. Detección de script nativo: NO por asociación (el pizzero idle ya tiene pose de saludo parcial
   blend 1.0, y BlendAnimation reutiliza la asociación de la misma jerarquía). Se observa el stream
   de opcodes nativos (hook FinishedOpcodeProcessing → PoolBeforeCommand): 0x0605/0x0812/0x070A de
   scripts no-misión sobre un ped de servicio/interior propio marcan actividad nativa por 2 s.
   El saludo es overridable; al ceder/limpiar solo se fade la anim nuestra si lo nativo está inactivo.
   Al primer apply se aborta el task de saludo (TASK_SIMPLE_NAMED_ANIM) si existe.
3. INTERIOR_HELD_OBJECT: el server estampa senderPlayerid y rutea solo a la sala del sender; el
   invitado descarta paquetes cuyo sender != dueño actual; objectid = (senderPlayerId << 24) |
   contador (único por dueño, sin colisiones en handover); limpieza al cambiar de dueño.
4. Handover: el nuevo dueño reconstruye los objetos sostenidos desde lo que vio como invitado
   (ms_lastKnown por pedid), reconciliando en PublishHeld (sin race con la gracia); el remove
   (modelId=0) borra lastKnown para no recrear lo que ya se soltó.
5. Ciclo de vida: NO se usa CTaskSimpleHoldEntity en copias (plugin-sdk es stub vacío; su destructor
   marca m_bRemoveFromWorld y el abort urgente suelta el objeto sólido al piso). En su lugar:
   seguimiento por frame del objeto al hueso con las funciones del juego (GetAnimHierarchyFromSkinClump
   0x734A40, RpHAnimHierarchyGetMatrixArray 0x7C5120, RpHAnimIDGetIndex 0x7C51A0; patrón CSceneFidget).
   Sin task no hay destructor/abort/slot-conflict; OnPedDestroyed limpia copias y objetos propios;
   MaintainCopies recrea al streamear el ped; timeout de pendientes 30 s.
6. Validación: server (area == area trackeada del sender, pedid con syncer==dueño + interiorPopulation
   + area + radio 150 m, throttle 150 ms por sender, CLEAR dedupe) y dueño (bone < 19 = m_apBones,
   modelId < 26316, |offset| < 5 m, flags sin translate, animBlock terminado, time/speed sanos).
7. Sala: todas las pizzerías comparten area 5 → el server rutea por area+150 m (regla de
   INTERIOR_POPULATION), no broadcast.
8. Dos keepers modelo 155 a 3.3 m: match por MENOR distancia (no primero), y se trackea por ped de
   servicio local (el que tiene pose/hold activo es el que atiende).
9. boneFlags viaja en el paquete (vanilla = 16 translation-only; con otro flag se replica la fórmula
   vanilla con rotación del ped).
10. Clamp de animSpeed: se acepta hasta 1000 (junkfud usa blend/anim rápidos); aplica también al
    camino nombrado existente (efecto preexistente en la compra del dueño).
11. Apagado en caliente: Process limpia copias/pendientes/overrides/objetos propios de inmediato.
12. Save/load: objetos del override se destruyen en CLEAR/timeout/Reset; Reset también al cargar
    (hook de carga si existe; si no, queda en la lista de prueba).
13. Scan con GetTaskHold(false) (cubre hold secundario + pickup/putdown activos), no
    FindActiveTaskByType exacto.
14. Copia creada en la posición del ped (sin flash en origen); flags de creación como CObjectSync.
15. Sin self-heal de slot (no hay task); el tracking por frame se reaplica solo.
16. F1 individual: el invitado pausa los UPDATE de servicio (CMissionMode::IsIndividual); el timeout
    de 1.5 s limpia el lado dueño.
