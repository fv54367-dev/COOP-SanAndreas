# TANDA 2026-11-10 — ideas y código de sa-coop (Karimov-Akbar, MIT)

> **Quién:** Claude orquesta, Codex escribe (`tools\correr_tareas.py`, una tarea = un commit SIN REVISAR hasta la revisión de Claude).
> **Referencia:** `_refs\sa-coop-karimov` (https://github.com/Karimov-Akbar/sa-coop, licencia **MIT**, clonado 2026-11-10). Se puede portar código: citar el origen en el commit (`Portado de sa-coop (Karimov-Akbar, MIT): <archivo>`) y agregar la entrada en `NOTICE.md` (texto de la licencia MIT de ellos, una sola vez). **No confundir con SACoop de Parricidium (sin licencia, solo ideas).**
> **Pedido del dueño (2026-11-10):** "todo lo que veas posible hacelo, siempre y cuando siga nuestras reglas". Pidió explícitamente: César Vialpando y Life's a Beach en coop, billar de a dos, desafíos de a dos (básquet, etc.), GPS con ruta activable desde F1, compañero en las cinemáticas.

## Qué NO se trae (choca con las reglas de `AGENTS.md`)
Su modelo es "la historia es del host": guardan el progreso/dinero/skills del compañero en el host, la población la genera solo el host, `mission-rules` deja que cualquier jugador cumpla los chequeos del host (= "cualquiera avanza"), estrellas compartidas. Nada de eso entra. De su código se toma solo la técnica de cada módulo.

## Decisiones del dueño que cambian requisitos
- **César Vialpando deja de ser solo individual** (antes `docs/REQUISITOS.md` y T45): pasa a coop e individual, con puntaje **en equipo** (los puntos del invitado se suman a los del dueño). El VS de baile/lowrider sigue descartado.
- **Billar de a dos: se retoma** (estaba en "Descartado, no retomar" de `ESTADO.md`).

## Tareas (en este orden; cada una con su interruptor, apagado = comportamiento de hoy)
| # | Tarea | Referencia de ellos | Protocolo |
|---|---|---|---|
| T53 | César Vialpando coop (lowrider en equipo) | `features/story/minigames/Minigames.cpp` | probable |
| T54 | Life's a Beach coop (baile en equipo) | mismo archivo | probable |
| T55 | Billar de a dos (POOL2, modo nativo de 2 pads) | `features/story/pool/` | probable |
| T56 | Desafíos de a dos (básquet y demás) | `features/story/contest/` | sí |
| T57 | GPS con ruta al punto del mapa, interruptor en F1 | `features/waypoint/` | no (salvo ver rutas de otros) |
| T58 | Compañero dentro de las cinemáticas (lugares por toma + clips de escuchar) | `features/story/cutscene-partner`, `cutscene-clips` | sí |
| T59 | Chat de voz por cercanía | `features/voice/` | sí |
| T60 | `/tp` y `/traer` con pedido Y/N; animaciones de a dos | `features/teleport/`, `features/anims/` | sí |
| T61 | Conexión sin VPN (UPnP + NAT punching, código de conexión) | `src/common/api/net`, server | sí |

Trenes: los hizo GLM (`TANDA_2026-11-10_glm_sync_trenes.md`, mismo diseño que `features/trains/Trains.cpp` de ellos); se revisan y commitean aparte. Manejar la copia del tren (su `TRAIN_DRIVE`/`TRAIN_ENTER`) queda para la T5 de esa tanda.

## Reparto y tablero (coordinado por Claude, 2026-11-10)
- PC del dueño (Codex local, en `main`): T53, T54, T55, T56, T58, T60.
- PC del amigo (`DESKTOP-HHVSGP3`, su Codex, rama `tanda/2026-11-10-amigo-sacoop`): T59, T61, T57.
- **En esta tanda el tablero se usa solo con archivos concretos, nunca con carpetas** (`client/` bloquearía a la otra PC). Los archivos compartidos (`packet_types.h`, `config.h`, `CConfigLoader.*`, `UI/CModeMenu.*`, `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `NOTICE.md`) los tocan las dos: está coordinado y Claude resuelve los conflictos al mergear la rama. Si `red reservar` dice "Choca con" por uno de esos archivos compartidos, seguí igual.

## Protocolo
Las tareas corren de a una. La que cambie el protocolo sube `COOPANDREAS_VERSION` en `shared/config.h` a la siguiente de la que encuentre (los trenes dejan 0.4.23-alpha), agrega los paquetes **al final** de `packet_types.h` y recompila `testbot`.

## Casos borde comunes (cada tarea agrega los suyos)
- 3–4 jugadores: todo diseño escala a 4 (si el original es de 2, como el billar, se dice qué pasa con el 3.º y el 4.º).
- Jugador en individual (F1): no participa ni recibe nada.
- Se desconecta o muere a mitad: regla de `AGENTS.md` (en misión coop, si cae uno falla para todos; abandonar saca solo al que abandona).
- Interior / otra punta del mapa: no se le aplica nada.
- Interruptor apagado: comportamiento exacto de hoy.

## Estado
(cada tarea agrega acá su resumen y críticas)

### T75 — Fable: máquinas fase 3, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- **New Model Army (ZERO4), hecha:** el dueño emite `MACHINE_PHASE` kind RC_GOBLIN (pos de la mesa ZERO4.txt 736, heading 90, area 10) mientras su `m_pRemoteVehicle` es el Goblin (`Rc.TakeCar` del script). Invitado (`CMissionWarTable`, nuevo): `GiveToy` lo pasa al interior (SET_CHAR_AREA_VISIBLE/SET_AREA_VISIBLE 10, parado junto a la BMX del dueño, LOAD_SCENE+REQUEST_COLLISION de la mesa, cámara fija sobre la mesa), `ToyPlace` acepta piso hasta 7 m bajo el toy del dueño (sa-coop), `Launch` pone el RC en area 10. Imán: MINI_MAGNET visual bajo el RC; Círculo levanta la copia de BARREL2/KMB_PLANK a 1 m debajo (Claim + ATTACH_OBJECT_TO_CAR con los offsets del script) y la suelta (DETACH + Unclaim). Vuelta: cuando `CMissionMachines::GuestToyPending()` (nuevo) da falso, CJ a la calle donde estaba, congelado hasta que hay piso (5 s). Lado dueño: `GUEST_OBJECT_MOVE` lleva bit `released` (0.4.38-alpha); al soltar, `Wake` (SetIsStatic false, sin esperar colisión, a la lista de movimiento) para que el barril real caiga sobre el tanque; un mover callado 1,5 s también suelta; si el script del dueño tiene el objeto adjunto (su propio imán), la posición del invitado no se aplica. Modelos por nombre con `CModelInfo::GetModelInfo`. Sin tocar `Back`/`TakeAway`/`OnDamage` (GLM).
- Límites: dos invitados sobre el mismo barril, manda el primero (el otro lo ve en su imán hasta soltarlo); la bomba RCBOMB solo la mueve el dueño (el script explota la que soltó su propio imán); entre reportes (100 ms) el barril real tiene gravedad: puede bajar unos cm hasta el siguiente.
- **Up, Up and Away! (HEIST5), hecha la torreta:** `AuthorityPhase` detecta el attach a #MINIGUN_BASE también en HEIST5 (HEIST5.txt 246/863) y emite TURRET como ZERO1; `PartnerUpdate` mide la cercanía contra la torreta del dueño (antes siempre contra el techo de Zero: a 300 m de la tienda nunca daba puesto). Los Hunter (114@/118@, vehículos de misión) mueren con `GUEST_MACHINE_DAMAGE` como los aviones de ZERO1. El cabrestante del Leviathan NO: no es "máquina del invitado" (un solo Leviathan 34@, `Heli.GrabEntityOnWinch` nativo y `Char.IsInCar($scplayer, 34@)` exigen al dueño adentro); un invitado puede ir de pasajero como hoy. La tarea T75 hablaba de "objeto colgado por el canal": el furgón es un vehículo, no un objeto; no aplica.
- **Customs Fast Track (STEAL4), NO hecha:** la grúa es `Crane.EnableControls` (CCranes/CRopes `PlayerControlsCrane`, PLAYER_ENTERED_DOCK_CRANE 079D) y su estado (giro, altura del gancho, qué lleva el imán) es local de cada juego: hace falta (1) paquete CRANE_STATE dueño→invitados aplicado al CCrane del invitado (layout de `CCranes.cpp` de gta-reversed, sin verificar), (2) paquete de pad del invitado que el dueño inyecte en `CCranes::UpdateCranes` (swap de CPad::NewState alrededor de la llamada), (3) cámara del invitado sobre la grúa. Los contenedores (Object.Locate3D de 106@ y Car.IsTouchingObject de los autos) sí contarían al mover la grúa del dueño.
- **Cop Wheels (HEIST4), NO hecha:** la carga a la Packer exige `Char.IsInCar($scplayer, moto)` + `Char.IsInAngledAreaInCar3D($scplayer, rampa)` + velocidades (HEIST4.txt 876-925) y luego `Car.AttachToCar(moto, camión)` + lo que el script haga con `$scplayer`: extenderlo a invitados (CMissionGuestCounts: IS_CHAR_IN_CAR 0x00DB e IS_CHAR_IN_ANGLED_AREA_IN_CAR_3D) hace que el dueño "esté" en la moto del invitado; habría que suprimir los opcodes siguientes sobre `$scplayer` (spot con hash como MANSIO2) y resolver la pelea entre el sync del invitado que la maneja y el attach del dueño. Antes hay que confirmar que un invitado pueda subirse a una moto MISSION_VEHICLE del dueño con traspaso de dueño del vehículo.

### T53 — Codex, SIN REVISAR / SIN PROBAR
- Implementado concurso por equipos con coop-lowrider=true, protocolo 0.4.24-alpha y atribución MIT en NOTICE.md; sin SCM, instalación ni VS.
- Juicio/BDISPLY local, reloj del dueño (también con stream local trabado), suma por sesión/beat y limpieza por participante.
- Crítica: la referencia supone host fijo y no separa intentos; se adaptó autoridad/participantes y se agregó sesión. Reloj negativo local no debe congelar flechas. La espera final está acotada a 3 s para no bloquear por desconexión.
- Client/server/testbot compilan. Pendiente revisión cruzada y juego; verificar hidráulica remota y distribución del dinero.

### T54 — Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- DANCE nativo solo desde MUSIC1, coop-dance=true, puestos por id, cámaras trasladadas desde dueño y juicio por reproduccion local. Reutiliza modulo/paquete T53, kind=2; protocolo 0.4.25-alpha.
- Puntaje acumulado deduplicado por participante/sesión, cierre con espera maxima 3 s, salida nativa en abandono/cancelaci?n. Clubes intactos; animacion remota pendiente de juego.
- Adaptado de sa-coop Karimov-Akbar (MIT), Minigames.cpp; licencia ya completa en NOTICE.md. Sem?ntica de hilo externo: gta-reversed StreamedScripts.cpp StartNewStreamedScript (0x470890).


### T55 — Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- `pool-two-players=true`, consentimiento F7 cercano en apuesta, sesión independiente del host de historia, segundo pad/mouse y turno nativo. Protocolo 0.4.26-alpha, COOP_POOL al final.
- VIEW de mesa existente, hasta 75 props con interpolación, cámara y avatar/taco nativos; tercero/cuarto espectadores. RESULT del nativo. Sin apuesta: START devuelve depósito del dueño; dinero ajeno intacto.
- Limpieza por F7/desconexión/individual/muerte, timeout 5 s, salida nativa 108@=1 y reset de handles al recargar. Referencia MIT Pool.cpp, licencia en NOTICE.md; semántica gta-reversed RunningScript/PadCommands/RpAnimBlend/Ped.
- Crítica: viejo CNetworkPadSync elegía primer remoto sin consentimiento ni mouse/mesa. Se conserva solo apagado; nuevo módulo excluye POOL2 del espejo genérico para evitar duplicación.
- Testbot f con cuatro clientes cubre consentimiento, remitente, tercer jugador, individual/lejanía, sesión vieja, snapshot máximo y liberación; resultados finales en tanda T55.

### T56 - Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- Desafios dirigidos de dos: basquet, blackjack/ruleta/rueda, Kickstart, Blood Ring y triatlon; carreras conservan CVsRace. activity-challenge=true, protocolo 0.4.27-alpha.
- Nativos independientes, dinero/estadisticas propios; critica: la referencia requiere entradas nativas y no separa nuestra autoridad de misiones. Se aislaron estadios/triatlon y validaron sesiones/consentimiento/resultado en servidor. Entradas de mesa/pelota pendientes de juego.
- Referencia MIT contest/Contest.cpp; pruebas y limitaciones en TANDA_2026-11-10_codex_t56.md.

### T70 — GLM: RYDER2 + canal de objeto movido, SIN REVISAR / SIN PROBAR / SIN INSTALAR (resto pendiente)
- Canal listo: `CMissionObjectMoves` (GUEST_OBJECT_MOVE; dueño único por objectid con vencimiento de 1,5 s, CObjectSync no sincroniza las reservadas, desconexión deja el objeto donde estaba). Robbing Uncle Sam es RYDER2 (GUNS1 es Home Invasion, ya cubierto por T69/T71): carretilla 530 propia por invitado junto a la del dueño; las cajas #WOODENBOX (1224) que empuja con las horquillas se trasladan al objeto real del dueño y su chequeo nativo Car.IsTouchingObject (RYDER2 4063/4077) cuenta y borra una sola vez. Las cajas de botín que se abren a tiros (62@/25) siguen del dueño: falta un canal de daño de objetos.
- Quedan fuera de T70: ZERO4 (interior área 10: cámara/piso + este mismo canal), grúa de Customs Fast Track (crane del script), cabrestante del Leviathan, Cop Wheels (entrada del invitado a vehículos de misión). Protocolo 0.4.36-alpha; testbot 9 escenarios/90 chequeos + (d) TODO PASS.
- Quedó pendiente: el canal compartido de "objeto movido por el invitado" (CObjectSync: dueúno único, sin pisar el sync mientras tanto, desconexión deja el objeto donde estaba) y las 5 misiones en el orden del archivo T70. GUNS1 (Robbing Uncle Sam) estudiado: sus cajas son objetos dinámicos en el interior área 8 con conteo por Char.LocateOnFootObject3D del dueño: mismo problema de interior que ZERO4, ninguna de las cinco es un recorte chico y limpio.
- La revisión solo lectura de todo lo del día quedó en tareas/REVISION_GLM_2026-11-10.md (4 altos / 10 medios / 12 bajas), incluidos dos de los propios sistemas GLM (CMissionMachines Back sin guard de muerte, CSocialActions Fade sin revalidar).

### T67 — GLM, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- `coop-mission-machines=true` (cliente): el dueño de la misión (cualquiera, no sólo el host) avisa su máquina por `MACHINE_PHASE` con latido de 2 s y vencimiento de 5 s en el invitado. Air Raid (ZERO1): ametralladora propia junto a la del dueño (base invisible 1485, mismo attach de ZERO1 739, spots por rank de invitado para 3-4 jugadores). Supply Lines (ZERO2): RC Baron propio en el mismo techo (kRoof verificado en ZERO2.txt 107-109/196), camera TakeControl de nuevo si una escena la roba, lejos de la tienda se lo trae a la puerta.
- Lo que la máquina del invitado hace cuenta: el invitado reporta la vida de su copia de cada vehículo de misión compartido (`GUEST_MACHINE_DAMAGE`, 150 ms por vehículo, sólo decreciente) y el dueño aplica la diferencia con el CVehicle::InflictDamage del propio juego (0x6D7C90, arma y muerte nativas: ZERO1 1050 HasBeenDamagedByWeapon y ZERO2 Car.IsDead cuentan solos, una sola vez).
- Port MIT Karimov-Akbar/sa-coop `features/story/rc-toys/RcToys.cpp`; joinpoint de máquina leído del script sincronizado en curso (nombre) y del attach del dueño (CPed::m_pAttachedTo, 0xFC). Protocolo 0.4.50-alpha: MACHINE_PHASE y GUEST_MACHINE_DAMAGE al final. Testbot: 8 escenarios / 81 chequeos + (d), TODO PASS.
- **Fuera de T67 (no forzadas), qué falta por misión:**
  - New Model Army (ZERO4): la mesa es interior (area 10): hace falta el port de cámara/piso de area 10 de RcToys (kParked/kTableView/FloorWait) Y un canal de "objeto movido" para que los barriles que el invitado lleva con el imán se muevan/cuenten en el dueño (los objetos de misión se ven por CObjectSync, pero mover la copia no mueve la del dueño).
  - Customs Fast Track: la grúa es un CScriptedCrane del script del dueño: hace falta rutear la entrada/controles del invitado al crane del dueño y confirmar los contenedores compartidos.
  - Up, Up and Away!: torreta propia es la parte fácil (igual que ZERO1); el cabrestante/imán del Leviathan necesita sync de cable/objeto colgado; sin eso el invitado no transporta barriles que el dueño vea.
  - Robbing Uncle Sam: las cajas existen y se ven (CObjectSync); falta que empujarlas/apilarlas con la carretilla de un invitado mueva la caja del dueño (mismo canal de objeto movido que ZERO4).
  - Cop Wheels: la moto que entrega el invitado es una copia; hace falta sync de entrada del invitado a vehículos de misión del dueño y que la carga al camión cuente en el dueño.

### T57 — GLM, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- `gps-route=true` (cliente, menú F1): mientras se conduce un vehículo de calle (auto/camión/quad/moto/BMX), el radar dibuja la ruta por calles hasta el punto propio del mapa (roja, debajo de los blips). Búsqueda con el buscador propio del juego (CPathFind::DoPathSearch 0x4515D0, FindNodeClosestToCoors 0x44F460) una vez por segundo (250 ms si cambió el punto o se salió de la ruta >40 m), con pedido de áreas de nodos 1,5 km adelante (MakeRequestForNodesToBeLoaded 0x450D70 / ReleaseRequestedNodes 0x44DD00) y ruta progresiva a mitad/curto si el destino queda lejos (más de 4950 nodos o áreas sin cargar).
- Dibujo enganchado al sitio de llamada del salto de cola único 0x58AA2D → CRadar::DrawBlips (verificado en el exe con capstone: las dos ramas de CHud::DrawRadar convergen ahí), con `Events::drawBlipsEvent.before`. Se omite en mapa grande del menú (m_bDrawingMap 0xBA67A1), radar sólo blips (m_nRadarMode 0xBA676C), interior (CGame::currArea), cinemática o sin punto; y no busca si el radar lleva 1 s sin dibujarse (HUD oculto). Bandas con CSprite2d::Draw2DPolygon y transformaciones de CRadar de plugin-sdk (mismas direcciones de gta-reversed).
- Decisión: sin ruta hacia puntos de otros jugadores (ya se ven sus puntos por PLAYER_PLACE_WAYPOINT que ya viaja por red; la ruta queda sólo para el punto propio del dueño). Sin protocolo nuevo; versión queda 0.4.29-alpha.
- Compilan client/server/testbot. Falta revisión cruzada y juego.

### T60 — GLM, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- `tp-request` (server, default true): `/tp`, `/ir` y `/traer` por nombre fuera del modo prueba con Y/N a 15 s. El server lleva sesion unica por jugador, caduca, revalida al aceptar (vivos, coop, a pie, mismo area, cercania en pareja, sin mision) y ancla posiciones en su propio estado (nunca en el paquete). En modo prueba siguen los comandos viejos sin consentimiento.
- `emotes` (cliente, default true): F9 (U tomado por T56) o `/anims`; ventana ImGui sin mouse, flechas/Enter. Animaciones propias del juego + 16 de a dos; invitado y anfitrion se colocan enfrentados por geometria del server. Pose remota por latido `EMOTE_STATE` (bloque + hash), misma via del sync de animacion con nombre existente.
- `.ifp` propios en `coopnims`: leidos y validados antes de entregarlos al juego (ANP2/ANP3, conteos y tiempos sanos), bloque permanente propio `C_XXXXXXXX`, nunca en streaming ni sobre bloque temporal de cinemática; faltante remoto omite la pose.
- Protocolo 0.4.29-alpha: SOCIAL_ACTION y EMOTE_STATE al final de packet_types.h. Testbot escenario (i): 15 chequeos de consentimiento dirigido, respuesta vieja, tercero, individual, mision entre oferta y aceptacion, pareja, poses a espectadores, cancelacion y desconectado; los 8 escenarios (81 chequeos) TODO PASS.
- Fix durante la tanda: el server solo guardaba m_bOnMission del host (ON_MISSION_FLAG_SYNC); ahora CNetworkPlayer lleva m_nRunningMission desde PLAYER_MISSION_MODE (que cualquier jugador manda al entrar/salir de mision) y SocialReady bloquea con cualquiera de las dos. El testbot ya simula la mision del invitado por esa via.
- Referencia MIT Karimov-Akbar/sa-coop `features/teleport/`, `features/anims/`; sin animaciones de terceros. Falta revision cruzada y juego.

### T58 — Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- Port ScenePartner/SceneMocap MIT, lugares/caminos embebidos, incluye WAY y CAR. Protocolo 0.4.28-alpha, CUTSCENE_PARTNERS al final; default cutscene-partners=true.
- Crítica: autoridad de misión, READY por escena/cercanía, sesión/secuencia/toma, latidos, filtro individual y limpieza explícita; T48 mantiene protagonista propio con sustitución de dueño en puesto del espectador. Clips en memoria y fallback sin cuts.img; nunca cargar COOPCS encima del bloque temporal de escena. Ver tanda T58 para verificaciones.
### T59 - alcance y crítica previa (Codex amigo)
Archivos: shared/config.h, shared/network/packet{,_types}.h, shared/network/packets/voice.h; client/src/CVoice.*, CVoiceAdpcm.h, CConfigLoader.*, Main.cpp, Debug/CImGui.cpp, UI/CModeMenu.cpp, PacketHandlers/voice.cpp; server/src/ConfigManager.*, CNetwork.cpp, CNetworkPlayer.h, PacketHandlers/voice.cpp; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md y esta tanda.
Crítica: negociar permiso del servidor antes de abrir micrófono; nunca confiar en identidad/rango enviados por el cliente. Limitar tramas y colas; descartar audio viejo al soltar, desconectar o cambiar de área.
Casos: 3-4 voces se mezclan; individual F1 queda excluido según esta tanda; desconexión/muerte cierran captura; áreas distintas y posiciones viejas no se reenvían. Alt-tab y pausa cierran micrófono incluso sin frames del juego. Apagado no abre micrófono ni envía voz. Sin dispositivo avisa una vez.

Alcance adicional T59: client/src/CPacketBuffer.cpp (voz inmediata, sin cola de interpolación del mundo).

### T59 - Estado final (Codex amigo, SIN REVISION CRUZADA / SIN PROBAR)
- Hecho: voz mono 16 kHz/20 ms con IMA ADPCM MIT, WinMM en hilo propio; mezcla 3-4 voces, colas acotadas y rechazo de secuencias repetidas/viejas.
- F4 mantenida para hablar (reservada frente al menu de trucos); F1 silencio general y nombres de hablantes en HUD. Sin microfono abierto automatico.
- Servidor: voice-chat=true, voice-range=50 (1-500 m), misma area, posiciones recientes, identidad impuesta y limite 60 tramas/s; clientes no capturan antes de negociar permiso. Individual excluido por regla comun de esta tanda.
- Cliente: voice-chat=true; apagado/mute/muerte/arresto/desconexion bloquean voz. Foco, tecla fisica y latido del juego comprobados por el hilo de audio; pausa/alt-tab cierran captura. Sin dispositivo/permisos avisa una vez.
- Protocolo 0.4.40-alpha; VOICE_SETTINGS/VOICE_FRAME al final del enum, canal VOICE no confiable. NOTICE.md conserva MIT completa y atribucion del port.
- Compilan client (-r, 32.657 s), server (-r, 7.844 s) y testbot (-r, 2.657 s); CRLF y git diff --check OK. Sin instalar, abrir GTA, reservar tablero ni escribir con git.
- Criticas/limites: area = m_nAreaCode del SDK + distancia (no hay identidad adicional de interiores). Mezcla con saturacion PCM; calidad, latencia, eco/realimentacion y compatibilidad de drivers WinMM requieren prueba real, no quedaron validados por compilar. La referencia tiene siempre-abierto configurable; aca se conserva solo pulsar para hablar, como pide T59.
- Pendiente: Claude revisa diff y commitea; usuario prueba los casos agregados a TESTEAR_HOY.md. No se ejecuto testbot contra servidor ni se probaron dispositivos de audio.

### T61 - alcance y critica previa (Codex amigo)
Archivos: server/src/CInternet.{cpp,h}, ConfigManager.{cpp,h}, Main.cpp, CNetwork.cpp; shared/ConnectionCode.h; client/src/CNetwork.{cpp,h}, CConfigLoader.cpp; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md y esta tanda.
Critica: no aceptar lease permanente; no reemplazar mappings ajenos; renovar con lease finito y limpiar antes de quick_exit. IP privada/CGNAT no garantiza acceso. Punching necesita se?alizacion externa: fase 2 con TURN.
Casos: 3-4 jugadores/F1/muerte/interiores no cambian transporte; desconectar no cierra puerto del server. Test-commands o puertos 6799/6800 omiten UPnP; apagado conserva VPN/IP. Sin router o conflicto se avisa y sigue por IP. No cambia protocolo.

Alcance adicional T61: client/src/CCustomMenuManager.cpp (entrada de letras/guiones y direccion:puerto, hasta 32 caracteres).

Alcance adicional T61: tools/tests/connection_code.cpp (regresion aislada del codec, sin red/GTA).

### T61 - Estado final (Codex amigo, SIN REVISION CRUZADA / SIN PRUEBA REAL)
- Hecho: UPnP IGD SSDP/SOAP en hilo propio; upnp=true por defecto, puerto UDP propio y lease 3600 s renovado cada 30 min. Sin fallback permanente, sin dependencias nuevas. IP externa consultada al router, sin servicios de terceros.
- Mappings existentes no se reemplazan: exige GetSpecificPortMappingEntry=714 antes de crear. Identificador por proceso/arranque e IP/puerto internos se verifican antes de renovar/borrar. Stop se invoca antes de quick_exit (Ctrl+C/cierre de consola); crash depende del lease del router.
- Test local (test-commands=true, puertos 6799/6800) y upnp=false no intentan descubrimiento. Fallo de router/conflicto/CGNAT se anuncia y ENet sigue disponible por VPN/IP.
- Cliente: menu IP / code y clave ip del ini admiten codigo base32 XXXX-XXXX-XXXX, prefijo SA-, IPv4 y IPv4:puerto. Entrada de hasta 32 caracteres; codigo/endpoint imponen su puerto incluso con casilla port=0. El codigo revela direccion, no autentica ni cifra.
- Sin cambios de paquetes/version: conserva 0.4.40-alpha de T59. Port MIT de src/common/api/nat-traversal/NatTraversal.{cpp,h}, referencia local C:/Users/lucia/refs/sa-coop-karimov; NOTICE.md actualizado. No gta-reversed en esta PC; no se agregan hooks/offsets.
- Compilaciones completas -r: client 31.625 s, server 8.015 s y testbot 2.640 s OK. Test aislado tools/tests/connection_code.cpp: PASS (9999 endpoints y rechazo de entradas malformadas/version/puerto o IP cero/key ajena). CRLF en todos los cpp/h y git diff --check OK.
- Fase 2: NAT punching necesita intercambio de endpoints/senalizacion (la referencia usa pub/sub externo); TURN necesita relay y credenciales. No entran limpios sin infraestructura externa: quedan sin implementar, no se promete salvar CGNAT/doble NAT.
- Criticas/limites: IPv4 y HTTP simple del router; solo endpoints de control numericos, descubrimiento por interfaz de ruta predeterminada. Routers sin consulta de mappings/lease finito se rechazan. IGD puede no respetar su lease: hay que medir expiracion real; cierre forzado puede interrumpir limpieza. Cambios de IP WAN requieren reiniciar para emitir codigo nuevo. No probados routers, firewall, acceso externo, renovacion ni cierre real.
- Pendiente del orquestador: revisar diff y commitear. Usuario prueba TESTEAR_HOY.md. No instalado, no GTA abierto, no testbot ejecutado contra server, no reservas ni escrituras con git.

### T65 — Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- Fotos nativas de peds/objetos del invitado al dueño, `coop-photos=true`, MISSION_PHOTO fiable y protocolo 0.4.31-alpha. MIT Objects.cpp; semántica gta-reversed TakePhotograph y consumo de flags; sin SCM.
- Adaptado a autoridad/participantes/epoch y misma área. Agrupa duplicados 500 ms y permite repetir fotos descartadas; condiciones extra de SYN1 y ajuste del reloj pendientes de juego. Sin copia no cuenta. Testbot i cuatro clientes: 9 PASS. Detalle y pruebas en tanda codex_t65.
