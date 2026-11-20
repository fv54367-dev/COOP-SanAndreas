# REVISIÓN GLM 2026-11-10 — solo lectura (hallazgos, sin cambios de código)

> Alcance: `git diff 519c91f..HEAD` (T53-T68 + T57/T60/T67 de GLM) sobre los archivos pedidos, más chequeo de
> colisiones de hooks y de reglas de `AGENTS.md`. Método: lectura de los diffs y del árbol actual, hallazgos
> verificados línea a línea a mano (no se reportó nada que no esté en el código). Nada fue modificado ni corregido
> en este paso: cada punto queda para quien lo arregle (Codex u orchestador). Líneas del árbol actual.
> Reglas rotas: ninguna de dinero/estrellas ni de "uno arranca para todos"; sí hay una de "no mover al CJ"
> (hallazgo 2). Colisiones de hooks: solo la del hallazgo 14 (previa a hoy). El hook nuevo del tren (0x43426D,
> T62) verifica los bytes antes de parchear y no pisa a nadie.

## Altos

1. **server/src/CInternet.cpp:71-72 (T61)** — el parser XML de UPnP puede lanzar `std::out_of_range`: en el
   fallback de prefijos, `lx.rfind('<', p)` puede dar `npos` y `lx.substr(a, ...)` con `a == npos` lanza. La
   respuesta viene de cualquier dispositivo LAN que conteste el SSDP (entrada no confiable, cuerpos de error sin
   XML son normales) y `UpnpMapPort` corre en el worker de `Start()` (:329-340) sin try/catch: la excepción sale
   del hilo y `std::terminate` mata el server entero (cae la sesión de todos). **Arreglo:** tras el `rfind`,
   `if (a == std::string::npos) return "";`, y envolver SSDP+SOAP del worker en try/catch que loguee.

2. **client/src/CMissionMachines.cpp:127-157 (T67, regla rota)** — `Back()` mueve y reequipa al CJ sin ningún
   guard de muerte/vehículo/cinemática, y `TakeAway()` la llama en todos los caminos de caída (`!Connected`,
   interruptor apagado, latido vencido :328, cambio de fase :451, `Reset()` :410). Si el invitado muere con la
   torreta puesta (las bombas de Berkley lo matan aunque tenga pruebas), el cadáver es teletransportado a
   `own.stood` (0x00A1) y despegado (0x0465); igual si se desconecta a mitad. Viola "el CJ del invitado nunca se
   mueve muerto/en vehículo/en cinemática". **Arreglo:** en `TakeAway`/`Back`, si `m_fHealth <= 0`, cinemática o
   `bInVehicle`, diferir la limpieza (pendiente hasta revivir) o al menos saltar 0x00A1 y 0x0465.

3. **client/src/CSocialActions.cpp:236, 338, 341-352 (T60)** — `r->m_pPed` se usa en los caminos de Fade/Blend
   sin revalidar pool + `IsVTableValid()`: el ped remoto puede haber sido borrado (leave/stream-out) entre frames
   y el slot puede reutilizarse; `anim::Blend` escribe en el clump de memoria liberada ("crashes al azar"). El
   patrón correcto ya existe en el repo (`CBulletImpactForceGuard.h:26`, `CMissionGuestCounts.cpp:25`).
   **Arreglo:** revalidar con lookup de pool + vtable justo antes de cada Fade/Blend y saltar si falla.

4. **client/src/CMissionMachines.cpp:461-469 (T67)** — `OnDamage` llama a `InflictDamage` (0x6D7C90) sobre
   `m_pVehicle` chequeado solo con `IsVTableValid()` (que no detecta memoria de pool liberada sin reutilizar);
   el reporte llega por red y el vehículo pudo borrarse el frame anterior. Lo mismo en la lectura de
   `ReportDamage` (:380, ahí solo lectura). **Arreglo:** `CPools::ms_pVehiclePool->IsObjectValid(vehicle)` +
   vtable inmediatamente antes de la llamada.

## Medios

5. **client/src/CVoice.cpp:89, 104, 122, 224 (T59)** — `micFailed` se latchea para siempre (nunca se resetea):
   un fallo transitorio del micro (Discord u otra app tomándolo) mata la voz hasta reiniciar el juego, y `warned`
   tampoco se repone. **Arreglo:** resetear `micFailed`/`warned` por sesión en `Configure()` o reintentar
   `waveInOpen` cada ~5 s.

6. **client/src/CCoopLowrider.cpp:446 (T53)** — la posición del dueño que llega por red no se valida finita (solo
   el heading): un NaN pasa el `(pos - packet.pos).Magnitude() > 80.0f` (comparación falsa) y va directo a
   CREATE_VEHICLE/SET_CAR_COORDINATES del invitado. T54 sí agregó el chequeo para su camino (:609); falta el de
   T53. **Arreglo:** `std::isfinite` de pos en `StartLowrider`.

7. **client/src/CCoopLowrider.cpp:723, 727, 765 (T53)** — el watchdog de 5 s de los invitados corre también entre
   PRELOAD y PLAY: `PRELOAD` hace return temprano sin refrescar `g_lastPacket` (:723), así que si el dueño tarda
   >5 s (carga del modelo, cámara), el invitado aborta solo y libera la espera de todos; el timeout real de 30 s
   (:623) queda inalcanzable. **Arreglo:** refrescar el watchdog con PRELOAD (y/o no abortar antes de
   `g_part.play`).

8. **server/src/PacketHandlers/scripts.cpp (handler COOP_LOWRIDER ~:246; también COOP_POOL) (T53/T54)** — sin
   tabla de sesión server-side (a diferencia de `poolSessions` de T55): cualquier jugador en la misma área puede
   mandar JOIN/POINTS/DONE con sesión adivinable y el único filtro es cliente; los puntos del invitado entran
   directo al score del dueño, y en baile se aceptan saltos de hasta 1.000.000 por paquete
   (client/src/CCoopLowrider.cpp:591). **Arreglo:** sesiones en server al nivel de T55 (vincular
   dueño/invitados/sesión, caducidad y revalidación al aceptar).

9. **client/src/CCoopPool.cpp:493 + server poolSessions (T55)** — atasco de partner: un JOIN que el server
   aceptó pero cuyo dueño nunca inicia deja `s.partner` ocupado sin vencer (el OFFER del dueño renueva el tick),
   y ni el server ni el cliente aceptan re-JOIN (`s.partner.valid()` / `!g.partner.value()`): ese invitado no
   vuelve a entrar hasta que el dueño empiece o cierre. **Arreglo:** expiración propia para el partner reservado
   (p. ej. 5 s sin START).

10. **client/src/CMissionMachines.cpp:134-141, 165 (T67)** — el arma pesada guardada con 0 balas se pierde para
    siempre (solo se restaura si `heavyAmmo > 0`; el attach la reemplaza y el REMOVE borra el slot) y
    `SET_CHAR_PROOFS off` pisa pruebas que pudo poner un script. **Arreglo:** restaurar siempre que
    `own.heavy > 0` (con su munición, aunque sea 0) y guardar/restaurar los flags de pruebas exactos.

11. **client/src/CSocialActions.cpp:220-227 + shared/network/packets/scripts.h (Serialize de SocialAction) (T60)**
    — `pos`/`heading` de red llegan a `Move()` (LOAD_SCENE + SET_CHAR_COORDINATES) sin isfinite del lado cliente;
    hoy el server los escribe él mismo, pero el cliente confía en el cable y los paquetes hermanos de T67 sí
    validan. **Arreglo:** isfinite en `OnPacket` y en el `Serialize` (por paridad con mission_machines.h).

12. **client/src/CMissionMachines.cpp:468 + shared/network/packets/mission_machines.h:62 (T67)** — `p.weapon`
    (serializador 0..64) entra crudo como `eWeaponType` al engine: los tipos reales terminan ~46 y entradas
    basura deciden explosión/sangre con un `CWeaponInfo` inválido. **Arreglo:** en `OnDamage` forzar
    `kGun` (lo único que se manda) o rechazar `> 46`.

13. **client/src/CCutscenePartners.cpp:178 (T58)** — `ped->m_nAreaCode=FindPlayerPed(0)->m_nAreaCode;` sin
    null-check dentro del hook de render (0x53E9F9): crash instantáneo si el ped local no existe a mitad de
    cinemática; 13 líneas después (:191) sí se chequea. **Arreglo:** null-check igual que en `HandlePacket`.

14. **client/src/Hooks/VehicleHooks.cpp:735-741 contra 768-775 (previo a hoy, hallado en el chequeo de
    colisiones)** — siete call sites (0x6B51CD, 0x6B52A2, 0x6BCC64, 0x6BCD7B, 0x6C1B72, 0x6C1C5B, 0x6F1925) se
    parchean dos veces: primero `CCarAI__UpdateCarAI_Hook` y después `CCarCtrl__UpdateCarAI_Hook` (el último
    gana): el hook de CCarAI hoy nunca corre en ningún sitio. Los cuerpos son casi equivalentes (aplicar
    gas/freno/volante de un NPC no sincronizado), por eso no se nota, pero difieren en que el de CCarCtrl exige
    que el vehículo esté registrado en red: para vehículos de misión con conductor de red no registrado el
    comportamiento actual cambió sin querer. **Arreglo:** unificar en un solo hook (o borrar la primera lista y
    documentar el ganador).

## Bajas

15. **client/src/CVoice.cpp:81, 217-220 (T59)** — `GetForegroundWindow()` casero y el HWND cacheado una sola vez
    al arrancar el hilo de audio: si el juego recrea la ventana (reset de dispositivo), F4 muere en silencio; la
    regla del repo pide `CTestMode::IsGameWindowFocused()`. **Arreglo:** usar el helper y leer el HWND por pasada.

16. **server/src/CInternet.cpp:336-338, 342-346 (T61)** — el worker renueva el lease con el mutex tomado (SOAP
    bloqueante ~8-16 s): `Stop()` queda esperando ese tiempo; además `stopping` nunca vuelve a `false` (un
    `Start()` posterior quedaría muerto; hoy inalcanzable). **Arreglo:** renovar fuera del lock (copiar mapping,
    soltar, renovar, retomar) y resetear `stopping` al entrar en `Start()`.

17. **client/src/CMissionGuestActions.cpp:31, 359-377 (T66)** — entradas de `inputs`/`lastAnimation` de
    jugadores desconectados nunca se borran a mitad de sesión (estado muerto acumulable; `Eligible` los saltea,
    no crashea). **Arreglo:** en `Process()` (rama autoridad) borrar los ids cuya `GetPlayer(id)` dé null.

18. **client/src/CMissionMachines.cpp:451 (T67)** — `TakeAway(FindPlayerPed(0))` con ped null pasa un ref basura
    a 0x0465/0x0108/0x00A1 (y borra la base igual). `Reset()` (:410) ya lo hace bien. **Arreglo:** mismo guard
    `if (auto me = FindPlayerPed(0))`.

19. **client/src/CMissionMachines.cpp:176-186 (T67)** — `FloorWait()`/`floorUntil` son código muerto (nunca se
    asigna; quedó del port sin el camino de área 10). **Arreglo:** borrar o cablear en T70.

20. **client/src/CGpsRoute.cpp:108-131 (T57)** — si un script pide nodos con `LOAD_PATH_NODES_IN_AREA` mientras
    el nuestro está pendiente, pisa el rect y `OurRequest()` deja de matchear para siempre: nuestro
    `ReleaseRequestedNodes` no corre nunca (queda el upkeep de 350 m del juego; no crashea). **Arreglo:** token
    propio (rect + marca de generación) o liberar cuando `requested` y el juego ya no tiene pending.

21. **client/src/CSceneMocap.cpp:268, 353, 400 (T58)** — `FILE*` del worker de cuts.img queda abierto si
    `Make()` lanza (bad_alloc de los vectores grandes): leak solo por excepción, buffers sí se liberan.
    **Arreglo:** RAII (`unique_ptr` con `fclose`) o try/catch dentro de `Make()`.

22. **client/src/CActivityChallenge.cpp:126, 150, 226 (T56)** — globals de SCM con offsets fijos sin chequear
    `ScriptSpace` (mitigado: `Ready()` exige `ms_bVanillaScripts`) y score por resta de uptime en `int`
    (basura tras 24,8 días de uptime; cosmético). **Arreglo:** guard de null/size de ScriptSpace; limitar el
    score.

23. **client/src/COpCodeSync.cpp:1567-1600 (T63)** — `ProbeCondition` copia el script *shallow*: `m_pLocalVar`
    apunta a los locales del script real; seguro hoy porque los 7 opcodes del allowlist son de solo lectura, y
    `CommandHandlerTable[opcode/100]` se indexa sin el bounds-check que sí tiene el camino de red (:1519-1521).
    **Arreglo:** bounds-check + invariant documentado antes de tocar el allowlist.

24. **client/src/CTrainSync.cpp:555-563 (T62)** — `RestoreKeptTrains` recorre `ms_aKeptHeads` sin
    `IsVTableValid` (regla del repo; en la práctica los protege el flag de misión recién puesto). Y
    **shared/network/packets/train.h:135**: el campo `reset` del snapshot se serializa pero ningún cliente lo
    lee (semántica documentada sin implementar). **Arreglo:** revalidar punteros; implementar o quitar `reset`.

25. **client/src/CMissionPhotos.cpp:114-133 (T65)** — ped/object no se revalidan después de
    `weapon->TakePhotograph(...)` (ventana UAF corta; la llamada es sincrónica y no corre el mundo).
    **Arreglo:** revalidar pool + vtable tras la llamada antes de leer flags.

26. **client/src/CCoopPool.cpp:562-564 (T55)** — INPUT se manda cada frame (~60 Hz) por canal confiable sin
    rate-limit en el server; y `keys[20]` (40 bytes) copia menos de los 48 de `CControllerState`: los últimos
    campos nunca se reenvían (truncamiento, no overflow). **Arreglo:** rate-limit server del relay de INPUT y
    tamaño exacto de estado.

## Verificado limpio (lo pedido, sin hallazgo)

- Decodificación ADPCM de voz (CVoiceAdpcm.h): límites correctos (164 bytes exactos, resize sin underflow,
  validación al leer en el paquete); hilos de voz con atómicos/mutex correctos; ningún callback de audio toca
  memoria del juego. Lectura/escritura de voz se detienen bien en muerte/arresto/individual/desconexión.
- UPnP: lease con vencimiento de respaldo y desmapeo solo del mapping propio.
- Guest actions (T66): no toca dinero ni mueve al CJ del dueño; excluye individuales de ambos lados; offsets de
  main.scm verificados por huella FNV antes de usarse.
- IFP propio (T60): triple guard de cinemática correcto, bloque permanente con ref antes de cualquier
  cutscene; loader acotado.
- GPS (T57): Beside con ruta de 2 nodos exactos seguro; el pedido de nodos nunca pisa el de un script; Draw sin
  ruta stale tras reconectar.
- Máquinas (T67): el vehículo personal de nadie puede recibir daño (doble guard MISSION_VEHICLE + syncing);
  llaves del menú de emotes con foco de ventana correcto; el menú no pisa DisablePlayerControls de scripts.
- Colisión de hooks: el hook nuevo del tren (0x43426D, T62) verifica bytes antes de parchear y es sitio nuevo;
  los eventos de plugin-sdk no se duplican en los archivos del rango.

## Conteo

Altos 4 · Medios 10 · Bajas 12. De hoy: 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 15-21, 25, 26. Previos ya
reportados al orchestrador en la sesión anterior y siguen sin corregir: 8, 9, 14, 22-24.

## Arreglado (T73) — 2026-11-10

Continúa la sesión cortada de Codex (su diff sin commitear, verificado hallazgo por hallazgo contra el árbol) y
agrega lo que faltaba (20 y el blindaje del pool de peds en fotos). Compilan client/server/testbot; testbot del
worktree TODO PASS (9 escenarios / 90 chequeos en handover=1 + escenario d en handover=0). Todo queda SIN REVISAR
hasta la pasada del orquestador.

- **1 (alto):** `XmlTag` devuelve "" si el `<` previo es npos y el worker de UPnP va en try/catch que loguea y
  deja el server vivo (sin VPN como camino de salida).
- **2 (alto):** `Back()`/`TakeAway()` con guard de pool+vtable, salud, vehículo, cinemática y wide-screen: difieren
  la limpieza en `own.pending` y `Process()` la reintenta cada frame hasta que CJ reviva. Si el slot del ped se
  reutilizó (pedRef capturado en `GiveTurret`/`GiveToy`), borra la base y descarta el estado propio.
- **3 (alto):** `ValidPed()` (pool + vtable) justo antes de cada Fade/Blend, en `Fade()` y en la rama de `Process()`.
- **4 (alto):** `OnDamage` y `ReportDamage` chequean `CPools::ms_pVehiclePool->IsObjectValid()` + vtable antes de
  `InflictDamage`/lecturas; también pool+vtable del ped del reportante.
- **5 (medio):** `micFailed`/`warned` se resetean en `Configure()` (por sesión de red).
- **6 (medio):** `StartLowrider` rechaza pos no finita (igual que el camino de T54).
- **7 (medio):** PRELOAD refresca el watchdog si ya hay concurso propio, y el aborto de `Process()` solo corre con
  `g_part.play`; el timeout real de 30 s por stage queda como red de seguridad.
- **8 (medio):** el server lleva `lowriderSessions` (dueño+invitados+kind, JOIN solo dentro de 30 s de began,
  POINTS/DONE solo de invitados joineados con el juego en curso); espeja los límites del cliente (beat creciente y
  <=500 por paquete en concurso kind 1, total acumulativo con salto <=500 en baile kind 2). Caduca a los 10 s sin
  latido o si el dueño se desconecta.
- **9 (medio):** partner reservado vence a los 5 s sin START en el server (`s.joined`) y en el cliente (`g.heard`):
  el invitado puede re-joinear y el dueño no queda atascado.
- **10 (medio):** el arma pesada se restaura siempre que `own.heavy > 0` (pide el modelo si no está cargado) y las
  pruebas (bullet/fire/explosion/collision/melee) se guardan en `GiveTurret` y se reponen exactas en `Back()`.
- **11 (medio):** isfinite de pos/heading en `OnPacket` y en el `Serialize` de SocialAction (paridad con
  mission_machines.h).
- **12 (medio):** `OnDamage` fuerza `kGun` como arma del `InflictDamage` (lo único que se manda).
- **13 (medio):** null-check de `FindPlayerPed(0)` en el hook de render de cinemáticas (igual que `HandlePacket`).
- **15 (bajo):** foco por `CTestMode::IsGameWindowFocused()` y HWND actualizado por pasada (`gameWindow` atómico);
  el hilo de audio ya no usa un HWND congelado de arranque.
- **16 (bajo):** renovación del lease fuera del mutex (`Stop` ya no espera el SOAP) y `stopping` se resetea al entrar
  en `Start()`; el map de UPnP se hace antes de tomar el lock.
- **17 (bajo):** `Process()` borra entradas de `inputs`/`lastAnimation` de jugadores que ya no están.
- **18 (bajo):** `TakeAway` soporta ped null y `OnPhase` marca `pending` si el ped local no existe.
- **20 (bajo):** `Request()` descarta el claim propio si el ciclo del slot terminó sin nosotros (un script tomó y
  soltó el pedido); sigue sin soltar nunca el pedido de un script. Verificado contra PathFind.cpp de
  gta-reversed: el slot es único y el rect perdido ya lo recoge el upkeep de 350 m (no había leak de nodos, solo
  estado stale que ahora se limpia).
- **21 (bajo):** el FILE* del worker de cuts.img va en unique_ptr con fclose (RAII ante bad_alloc).
- **25 (bajo):** revalidación pool+vtable de peds y objetos después de `TakePhotograph`, antes de leer flags
  (y null-check del pool de peds, como ya tenía el de objetos).
- **26 (bajo, parcial):** rate-limit del relay de INPUT en el server (acumula deltas de mouse con tope +/-200 y
  manda 1 paquete cada 20 ms) + isfinite de mouse. El tamaño de `keys[20]` se deja igual: los 4 campos que quedan
  fuera (`m_bChatIndicated`, `m_bPedWalk`, `m_bVehicleMouseLook`, `m_bRadioTrackSkip`, offsets 0x28-0x2E) no los
  usa el billar; agrandar el paquete subiría el protocolo sin beneficio visible (queda como propuesta).
- **No tocados (como pedía la tanda):** 14 (hooks dobles CCarAI/CCarCtrl, propuesta pendiente de decisión),
  19 (FloorWait muerto), 22, 23, 24.
