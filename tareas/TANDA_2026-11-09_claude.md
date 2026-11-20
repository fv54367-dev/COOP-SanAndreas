# Tanda 2026-11-09 (Claude orquesta, Codex escribe)
Reporte del dueño jugando la historia en scripts ORIGINALES. Sin abrir ni instalar en los GTA (lo hace el orquestador). Dueño del protocolo en esta tanda: T25 (sube a **0.4.19-alpha**; la versión se sube una sola vez).
El amigo trabaja con su Codex en una rama aparte: nada de esta tanda se hace en ramas ajenas.

## Casos borde (para todas)
3–4 jugadores; uno en individual (F1), tarde u otro modo de scripts no recibe ni cuenta; desconexión o muerte a mitad sin pendientes colgados; interior u otra punta del mapa; interruptor apagado = comportamiento actual; nunca durante `COpCodeSync::bProcessingNetworkOpcode`. Ante la duda sobre romper una misión: no implementar y explicarlo en el resumen.

## Puntos
| # reporte | Tarea | Estado |
|---|---|---|
| 2 marcador con auto no espera al host | T24 | revisado por Claude; sin probar en juego |
| 4 pistola con munición infinita | T25 | revisado por Claude; sin probar en juego |
| 9 el auto sale volando al disparar | T26 | revisado por Claude; sin probar en juego |
| 3 aliado enojado si le pega un invitado | T27 | revisado por Claude; sin probar en juego |
| 1 Ryder no sale del interior | T28 | revisado por Claude (se agregó el filtro bAcceptNpcGroup a mano); sin probar en juego |
| 6 enemigos de Sweet's Girl lejos del host | T29 | revisado por Claude; sin probar en juego |
| 7 auto de 4 puertas con invitado al volante | T30 | NO implementado: ver resultado abajo; decide el dueño |
| 8 individual de invitados y Cesar | T31 | solo log `solo-launch` y aviso de Cesar: sin causa demostrable; falta el log del invitado |
| 8b ver la misión individual de otro | fuera de esta tanda | los actores de una misión individual son locales a propósito (`WorldHooks.cpp:121`); necesita diseño propio |
| 5 cilindro rojo que debería ser invisible | T32 | revisado por Claude; sin probar en juego |

## Crítica previa y resultados
(cada tarea agrega acá sus 3–5 líneas)

### T24 - critica previa (Codex)
- LOCATE_CAR verdadero debe esperar sin IS_CHAR_IN_CAR previo ni CanUseGuestCar (incluye requiresHostCar).
- Con 3-4 participantes, exigir cercania incluso sin esfera; F1 individual y desconectados conservan el filtro existente.
- Reutilizar T23 con el auto consultado, conservando tolerancia/area, host muerto/arrestado, radio >20 e interruptor apagado.
- Mantener resultado crudo: gta-reversed RunningScript.cpp, UpdateCompareFlag (0x4859D0), aplica NOT despues del filtro.

Archivos T24 reservados: client/src/CMissionSyncRules.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni cambios en GTA. Aceptacion: client/server compilados, CRLF, diff acotado; resumen <=6 lineas.

Resultado T24: implementado, CRLF conservado y git diff --check OK; client 7.5 s, server -r 7.8 s. SIN REVISAR / SIN PROBAR / SIN INSTALAR.
Inicio: FilterMissionStart bloquea al invitado coop salvo salidas previas bAlone: Cesar (sweetCounter==6), F1 individual, todos los demas individuales o mision individual local. Cesar puede iniciar aunque F1 diga coop; informado sin modificar (fuera de T24).

### T25 - critica previa (Codex)
- El slot puede contener otra arma: exigir el tipo exacto antes de SetAmmo; no entregar armas ausentes.
- Municion cero tambien debe replicarse; int32_t y rango serializado 0..INT32_MAX admiten 30000.
- Con 3-4 jugadores conservar filtros de participantes, F1 individual, abandono y desconexion; sin colas nuevas por interior/distancia.
- Conservar mission-player-equipment apagado y bloqueo de replay de red; no replicar REMOVE_ALL_CHAR_WEAPONS.

Archivos T25 reservados (T24 terminado): shared/network/packets/scripts.h, shared/config.h, client/src/CMissionSyncRules.cpp, client/src/PacketHandlers/scripts.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. CRLF, sin instalar ni tocar GTA; aceptacion: client/server/testbot compilados, diff --check y resumen <=6 lineas.

Resultado T25: setAmmo al final del paquete/serializador, 017B capturado, SetAmmo nativo solo para el tipo exacto; logs tx/rx y rechazo de flags incompatibles. Server reenvia el paquete completo. Protocolo 0.4.19-alpha; BUILD intacto. Referencia gta-reversed CharacterCommands.cpp SetCharAmmo, Ped.cpp SetAmmo. Client 27.5 s, server 6.875 s, testbot -r 2.484 s OK; CRLF y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T26 - alcance previo (Codex)
Archivos reservados: client/src/PacketHandlers/players.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, interruptor, instalacion ni GTA.
- 3-4 jugadores y F1 conservan el flujo de disparos existente; no hay colas al morir/desconectar.
- Interior/distancia: el rayo local puede fallar; normal opuesta unitaria y brazo de impacto limitado a 15 m.
- Disparo de longitud cero/no finita: normal fija unitaria como ultimo recurso. Impactos en peds conservan pedPiece.
- Aceptacion: client/server compilados, CRLF, diff acotado; resumen <=6 lineas.

Resultado T26: SDK CColPoint no inicializa; estructura puesta a cero, resultado LOS conservado, normal finita de largo 0.9..1.1 o respaldo unitario (disparo degenerado: eje Z). Impacto en vehiculo a >15 m usa su centro.
Referencia gta-reversed Weapon.cpp:617: ApplyForce usa normal y brazo hitCP.m_vecPoint - posicion. Otras llamadas en PlayerHooks.cpp pasan el CColPoint del juego; no hay otro armado manual.
Client -r 34.984 s y server -r 7.188 s OK; CRLF conservado, diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T32 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionAreaMarkers.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni GTA.
- Filtrar solo SWEET4, conectado y mission-sync-rules habilitado; sin interceptar replay de red.
- Comparar centro XYZ y ambas medias anchuras de 20 m (tolerancia 0.1 m); no ocultar el marcador de 4 m ni otras misiones.
- 3-4 jugadores reciben la misma ausencia de dibujo; F1 individual conectado conserva LOCATE propio y estrellas propias.
- Muerte/desconexion/interior/distancia no agregan estado; desconectado o interruptor apagado conserva dibujo previo.
- Referencia: gta-reversed source/game_sa/Scripts/TheScripts.cpp:1862, HighlightImportantArea solo dibuja; no cambia comparacion.

Resultado T32: filtro previo al dibujo/envio, tabla de dos centros, tolerancia 0.1 m y medias anchuras de 20 m; LOCATE intacto. Client -r 31.485 s y server -r 6.594 s OK; CRLF y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T29 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni GTA.
- Identificar solo HOODS5/00FE por centro y las tres medias anchuras con tolerancia 0.1 m; no ampliar otras condiciones.
- Resultado crudo antes del NOT nativo; retorno directo antes de FilterGuestCar y esperas de marcador.
- Con 3-4 jugadores contar solo participantes incluidos vivos y misma area; F1 individual, abandono y desconexion quedan fuera por ShouldIncludeNetworkPlayer.
- Usar posicion del vehiculo si va dentro, caja inclusiva nativa; replay de red e interruptor apagado conservan resultado anterior.
- Referencia gta-reversed CharacterCommands.cpp:429/613 (GetCharCoordinates/LocateChar3D), Collision/Box.cpp:81.

Resultado T29: tabla HOODS5/00FE, tolerancia 0.1 m, caja nativa inclusiva con posicion del vehiculo; participante vivo/misma area, retorno antes de esperas. Interruptor mission-any-player-trigger=true, log limitado a 5 s. Client 22.0 s y server -r 6.547 s OK; CRLF y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T27 - critica previa y alcance (Codex)
Archivos reservados: client/src/Hooks/DamageHooks.cpp, client/src/Hooks/DamageHooks.h, client/src/Hooks/CrashfixHooks.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni GTA.
- AffectsPed se consulta antes del calculo de dano; Add debe rechazar antes de informar grupo/amigos, conservando guards de pool.
- Usar acquaintance del ped: enemigos Mission1 con Dislike/Hate no quedan protegidos por Respect del tipo.
- 3-4 jugadores: excluir autoridad como atacante incluso si es remota por traspaso; individual, abandono y desconexion no activan la regla.
- Interior/distancia no agregan estado; interruptor apagado y replay de red conservan comportamiento anterior.

Resultado T27: filtro previo en CEventGroup::Add y AffectsPed (25 call-sites verificados contra exe con capstone, mas vtable); guards de pool conservados, acquaintance POR PED y grupo, autoridad remota excluida por traspaso. Interruptor true y log cada 5 s. Referencias gta-reversed Event.h, CEventDamage::AffectsPed y CEventGroup::Add; IDs de historia originales 11..112 (main.txt). Client/server -r OK, CRLF y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T28 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni GTA.
- Ventana de 3 s por transicion, handles de pool y una recuperacion por ped; excluir jugadores, muertos y peds en auto.
- 3-4 participantes: solo autoridad de historia coop; individual, abandono, desconexion y replay cancelan estado.
- Ped remoto: PED_TAKE_HOST sin retorno al anterior, esperar asignacion antes de mover; no usar ClaimOnRelease como toma inmediata.
- El cierre de salida depende de mission-interior-safety: al salir la autoridad se reinicia entryWait y occupied difiere el cierre al invitado interior; no modificar rule-wait-interior.

Resultado T28: seguimiento por frame de scripts, 3 s por cambio de area, handles de pool; Teleport/FlushImmediately/area/enex como WarpGangWithPlayer. PED_TAKE_HOST existente sin retorno, warp solo tras asignacion. Interruptor true; no cambia rule-wait-interior ni protocolo.
Causa: el traspaso automatico de CHandover descarta MISSION_CHAR y CInteriorOwnership conserva los actores coop; no hay evidencia estatica de que el invitado se apropie de Ryder. Area vieja explica que no se dibuje, pero la causa exacta queda pendiente del log en juego. CEntryExitMarkerSync difiere SwitchEntryExit False mientras el invitado ocupa el interior, con mission-interior-safety=true y autoridad afuera (entryWait reiniciado).
Client -r 31.688 s y server -r 6.688 s OK; CRLF en C++ conservado y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T31 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/CMissionMirror.cpp, docs/MISIONES_MODO.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion, GTA ni SCM.
- Loguear antes de descartar comparaciones falsas: $onmission puede impedir llegar a 03EE; muestreo por lanzador cada 5 s, tambien fuera del marcador.
- Cesar se identifica al lanzar id 19, aviso solo despues de superar CMissionLock; no por contador remoto ni por entrar al marcador.
- 3-4 jugadores, F1, muerte/desconexion e interiores conservan reglas; no forzar autoridad, flags, limpieza ni actores locales.
- Sin causa estatica demostrada, aplicar solo diagnostico y aviso; mission-sync-rules apagado conserva el filtro anterior.

Resultado T31: sin causa estatica demostrable, solo solo-launch y aviso de Cesar despues del lock; reglas sin cambios. Muestreo de comparaciones de lanzadores antes de descartar false, cada 5 s por nombre (incluye fuera del marcador, counter=-1 fuera de SWEET); opcode/state para contexto.
Camino revisado: condicion onmission/LOCATE -> 03EE (resultado nativo) -> FilterMissionStart (lock, bAlone antes de autoridad) -> 0417 -> OnMissionLaunched/lock -> mision local detectada. SetIndividual limpia flag; receptor ON_MISSION_FLAG_SYNC protege local/individual; server Relay excluye individuales. SWEET6 no esta en aszSyncedScripts y mantiene actores locales.
Referencia gta-reversed PlayerCommands.cpp CanPlayerStartMission -> PlayerPed.cpp 0x609590: GameState, control, tareas primarias/secundarias, vida y EVENT_SCRIPT_COMMAND pueden negar 03EE. No prueba cual falla en la sesion reportada; falta log del invitado intentando marcador, contador propio y flag. No se fuerza ninguno.
Validacion T31: client -r 32.14 s, server -r 7.078 s OK; C++ CRLF sin LF sueltos y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo ni GTA ni SCM.

### T30 - critica y resultado (Codex)
Archivos reservados: esta tanda, TESTEAR_HOY.md y ESTADO.md; solo documentacion, sin C++, protocolo, instalacion ni GTA.
- No implementar la opcion de referencia: con 3-4 participantes un sedan de cuatro plazas no admite jugadores + Sweet + Kendl; incluso con 2 participantes se necesitan 3 pasajeros, pero el script acepta desde 2.
- HOODS5 borra ocupantes de asientos 0/1 antes de sentar a Sweet/Kendl; nunca poner ahi un jugador ni quitar HOODS5 de requiresHostCar. El host debe conservar un auto valido en 00D9 y al releer 59@ despues del fundido.
- PutPlayersInCar/rule-boarding reservan asientos de NPC que ya entran; no reservan esos futuros pasajeros antes del fundido, omiten jugadores ya en auto y no generan copias con mission-guest-car-copies=false (defecto).
- Cambiar conductor exige liberar el asiento remoto antes de tomarlo: server vehicles.cpp:120-146 rechaza al nuevo conductor si sigue ocupado y solo despues reasigna syncer. Un warp local no garantiza el traspaso; muerte/desconexion durante la espera requiere cancelacion.
- Alternativa propuesta, NO implementada: transicion especifica HOODS5 con reserva anticipada 0/1, liberacion confirmada del conductor y toma del host, invitado en 2 si existe, y autos de apoyo para excedentes (tambien cuando solo hay 2 plazas de pasajeros); mismos filtros F1/area/cercania e interruptor apagado. Requiere diseno propio y revisar llegada/limpieza con 3-4 jugadores.

Evidencia: scm/scripts/HOODS5.txt:846-888, 1000-1017, 1052-1060 y 1152 (SCM del repo archivado, consistente con la evidencia del original provista por T30). gta-reversed source/game_sa/Models/VehicleModelInfo.cpp:702, GetMaximumNumberOfPassengersFromNumberOfDoors (0x4C89B0): salvo excepciones devuelve puertas-1; Vehicle.cpp:434 copia ese limite a m_nMaxPassengers. CCommandPutNetworkPlayersInCar.cpp:147-239 y CMissionSyncRules.cpp:1090-1125 muestran que el reparto existente no resuelve la transicion. Resultado permitido expresamente por T30: NO IMPLEMENTADO, comportamiento conservado; falta revision del analisis y un diseno que soporte todos los participantes.

Validacion T30: client -r OK (33.703 s), server -r OK (6.672 s); git diff --check OK. Solo tres documentos modificados; C++/CRLF intactos. SIN REVISAR / NO IMPLEMENTADO / SIN INSTALAR; no se ejecuto el juego.

## Segunda vuelta (prueba en juego del 2026-11-09)
Dueño de esta vuelta: Claude. Sin cambio de protocolo.

| Tarea | Pedido | Estado |
|---|---|---|
| T33 | Nines and AKs: no dejar municion casi infinita | revisado por Claude; sin probar en juego |
| T34 | Balas de otros jugadores no empujan autos | revisado por Claude; sin probar en juego |
| T35 | OG Loc: motos extra para invitados | revisado por Claude; sin probar en juego |
| T36 | El aliado que entro con el host sale con el | revisado por Claude; sin probar en juego |
| T37/T38 | Drive-Thru: auto para invitados cerca del host | revisado por Claude (piso de referencia corregido a mano); sin probar en juego |

Punto 7 (auto de 4 puertas en Sweet's Girl): el dueño decidio dejarlo como esta.
Punto 8: causa encontrada, los invitados no tienen los lanzadores de historia (las partidas originales de GTA_JUGADOR2 no tienen el hilo `sweet` y el log no trae ningun `solo-launch`). Necesita diseño propio.

### T33 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- SWEET2 solo ajusta pistola a 10, 30000, 41@ y 5; excluir 30000 y conservar valores menores de 9000.
- Aplicar tras nativo solo a CJ de autoridad; conservar filtros de T25, replay, F1 individual y arma exacta por slot.
- 3-4 participantes reciben 60 por el paquete existente; muerte/desconexion/interior/distancia no agregan estado.
- Interruptor nuevo apagado conserva lo anterior; requiere mission-sync-rules y mission-player-equipment. Un inventario legitimo >=9000 tambien se reduce por el criterio pedido.

Resultado T33: ajuste local tras nativo y envio de 60 bajo mission-nines-ammo-repair=true; practica 10/30000 intacta, sin protocolo ni SCM. Referencia gta-reversed Entity/Ped/Ped.cpp:1098 SetAmmo y SWEET2:2897. Client OK (24.532 s), server -r OK (7.14 s); CRLF y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

### T34 - critica previa y alcance (Codex)
Archivos reservados: client/src/CBulletImpactForceGuard.h, client/src/PacketHandlers/players.cpp, client/src/Hooks/PlayerHooks.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- Replay remoto cancela impulso en todo vehiculo valido; local solo en el propio vehiculo ocupado, nunca en NPC ni peds.
- 3-4 jugadores y F1 conservan flujo existente; muerte/desconexion/interior/distancia no agregan estado pendiente.
- Validar pool antes/despues del impacto y conservar referencia para no restaurar una entidad eliminada o reutilizada.
- Restaurar ambas velocidades conserva dano/gomas/efectos; tambien cancela cambios de velocidad de una explosion causada por ese impacto, limitacion del enfoque pedido.
- Interruptor apagado conserva comportamiento anterior; log compartido entre ambos caminos, maximo una linea cada 2 s. Referencia gta-reversed Weapon.cpp:609-622.

Resultado T34: guard compartido restaura velocidades lineal/angular tras impacto nativo, valida pool/ref antes y despues; remoto todo vehiculo valido, local solo vehiculo propio ocupado. Interruptor remote-bullet-no-force=true, log compartido dv >0.05 cada 2 s. Client OK (23.703 s), server -r OK (7.422 s); CRLF y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo ni GTA.

### T35 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp y .h, client/src/CConfigLoader.cpp y .h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- CREATE_CAR devuelve el handle en ScriptParams[0] (StoreParameters(1)); validar modelo y posicion del vehiculo, no parametros de entrada ya sobrescritos.
- CWorld::Add_Hook -> CreateHosted envia VehicleSpawn tambien en receptor: una copia por pareja, manteniendo ParkCopiesNextTo por defecto igual.
- Contar solo invitados incluidos; 3 invitados producen 2 motos; F1 individual/abandono/desconexion quedan fuera del filtro existente.
- Evitar duplicacion cuando TWAR7 agrega tarde el blip de 36@; Freddy queda excluido por posicion. Interior/distancia no agregan colas.
- Interruptor apagado conserva comportamiento previo; blip y liberacion al subir/terminar reutilizan ProcessParkedCopies; falta prueba real con 3-4 jugadores.

Resultado T35: interruptor true, captura 00A5/TWAR7/modelo 461/posicion <=3 m; ParkCopiesNextTo(...,2), valor predeterminado 1 intacto. Registro de objetivo evita duplicar por blip tardio. Receptor crea vehiculo de red: WorldHooks.cpp:110 -> CNetworkVehicle::CreateHosted -> server vehicles.cpp:35 SendToAll. Sin protocolo.
Referencia gta-reversed RunningScript.cpp:670-685 StoreParameters; CREATE_CAR aun no reescrito en ese checkout: verificado con capstone sobre exe 1.0 US, handler 0x467AB3 llama CreateCarForScript 0x431F80, GetRef 0x424160 y 0x468298 escribe en 0xA43C78 antes de StoreParameters(1) 0x464370.
Client OK (23.969 s), server -r OK (7.219 s); CRLF conservado y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; no se tocaron GTA ni SCM. Blip solo en receptor elegido, segundo invitado ve y puede compartir su moto de red; reutiliza limpieza existente.

### T36 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA; resumen <=8 lineas.
- Recordar solo entradas con warp realizado; una solicitud de autoridad pendiente no demuestra que entro un aliado.
- Consumir el recuerdo en la siguiente transicion y limpiar al cambiar mision o perder elegibilidad; no reutilizarlo entre interiores.
- Consultar miembros actuales en cada frame: RemoveFromGroup durante escena excluye al aliado incluso si entro antes.
- 3-4 jugadores, F1, muerte/desconexion y switch apagado conservan filtros; log de transicion incluso con spawn rechazado o grupo ausente.

Resultado T36: recuerdo area/mision solo tras warp interior, consumido en la siguiente transicion; retorno a 0 omite filtro spawn, miembros actuales intactos. Log change con area spawn (-1 si ausente) y grupo lider. Sin tocar ProcessEntryWindow ni CEntryExitMarkerSync.
Client -r OK (36.656 s), server -r OK (8.484 s); CRLF C++ y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR. Referencia gta-reversed EntryExit.cpp:565-610.

### T37 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp y .h, client/src/CConfigLoader.cpp y .h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- Un auto de red creado por autoridad sirve a 1-3 invitados; registrar su blip en mirror para todos, no solo en el receptor creador de T35.
- SET_CAR_COORDINATES 00AB en SWEET3 por posicion, una vez por mision incluso sin invitados o al repetir escena; no depender del warp condicional.
- Buscar suelo y espacio para auto, excluir radio de 10 m del spawn enemigo; sin lugar valido no crear ni usar fallback inseguro.
- F1, abandono y desconexion conservan ShouldIncludeNetworkPlayer; sin pendientes por distancia/interior; interruptor apagado conserva lo anterior.

Resultado T37: un auto creado por autoridad (WorldHooks -> CreateHosted -> VehicleSpawn), compartido entre 1-3 invitados; mismo modelo/colores y heading 267.3. Blip registrado en CMirrorBlips con ScriptParams[0] preservado; ProcessParkedCopies libera al terminar.
Disparador 00AB/SWEET3/posicion <=3 m, consumido una vez por mision; sin lugar seguro no crea (cars=0). FindFreeCarPosition conserva sus callers anteriores y agrega busqueda estricta sin fallback, suelo y exclusion de 10 m del enemigo. Referencia gta-reversed World.cpp:2577/2732.
Client -r OK (32.75 s), server -r OK (7.078 s); CRLF C++ y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni GTA.

### T38 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA; resumen <=8 lineas.
- SWEET3:1078-1079 ejecuta coordenadas y heading consecutivos bajo el mismo guard; disparar solo tras 0175 y validar heading/posicion.
- Ocho candidatos relativos al auto girado, sin respaldo; validar tres suelos y diferencia maxima entre todos, LOS y volumen completo.
- 3-4 jugadores comparten un auto; F1, abandono, muerte/desconexion y otra area conservan filtros existentes, sin nuevos pendientes.
- Interruptor apagado conserva comportamiento; restaurar FindFreeCarPosition evita cambiar las otras misiones.

Resultado T38: disparador 0175 tras giro validado (tolerancia angular 0.01 rad); FindFreeCarPosition restaurada exactamente a antes de T37. Busqueda propia de ocho candidatos, suelo en centro/puntas, LOS y esferas solapadas longitudinales; sin respaldo. Logs no-spot o posicion elegida.
Referencia gta-reversed World.cpp:2577/2732/2829; orden SWEET3.txt:1078-1079 sin salto entre instrucciones. Client -r OK (38.062 s), server -r OK (7.5 s); CRLF C++ y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni GTA.

## Tercera vuelta (prueba en juego del 2026-11-09)
Dueño de esta vuelta y del protocolo: Claude.

| Tarea | Pedido | Estado |
|---|---|---|
| T39 | Cleaning the Hood: la puerta no queda cerrada si el host salió | revisado por Claude (ajustes a mano en T39/T40); sin probar en juego |
| T40 | Vehículos para invitados con subida automática (Drive-Thru, OG Loc) | revisado por Claude (ajustes a mano en T39/T40); sin probar en juego |
| T41 | Nines and AKs: subir a los invitados al volver de Emmet | revisado por Claude (ajustes a mano en T39/T40); sin probar en juego |
| T42 | Balas: sin empuje en la pantalla del que dispara | revisado por Claude (ajustes a mano en T39/T40); sin probar en juego |
| T43 | Lanzador de Sweet para invitados (punto 8) | probado OK por el dueño (build 2026-11-09.6) |

### T39 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp y .h, client/src/COpCodeSync.cpp, client/src/CConfigLoader.cpp y .h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- Recordar area 5 solo durante SWEET1B de autoridad coop; limpiar al cambiar mision o perder elegibilidad, incluso al repetirla.
- Capturar el cierre tras 07FB con host en area 0; no reabrir el cierre inicial ni el realizado adentro.
- Usar SetEnabledByName nativo: aplica bit 14 a todas las entradas con nombre LACRAK, incluidos ambos extremos si coinciden.
- Enviar correccion despues del Send normal de FinishedProcessingScripts; 3-4 jugadores conservan filtros ENEX existentes, F1 individual y replay excluidos; apagado conserva comportamiento.

Resultado T39: SetEnabledByName nativo tras 07FB cerrado en SWEET1B con autoridad afuera y entrada previa al area 5; recuerdo por mision/script, limpieza al perder elegibilidad. Correccion ENEX enviada despues del Send normal en FinishedProcessingScripts.
Referencia: gta-reversed EntryExit.h bit 14 y EntryExitManager.cpp:139; comando no reescrito en ese checkout, verificado con capstone: 0x4730A8 lee texto de 8 y ScriptParams[0]; 0x43F9B0 -> 0x156AE10 usa 0x4000, 0x43EF20 -> 0x15615C0 recorre pool y aplica flag por nombre a cada extremo coincidente.
Client OK (24.672 s), server -r OK; C++ CRLF y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni escrituras en GTA.

### T40 - critica previa y alcance (Codex)
Archivos reservados: client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp y .h, client/src/CMissionSyncRules.cpp, client/src/PacketHandlers/scripts.cpp, client/src/CConfigLoader.cpp y .h, shared/network/packets/scripts.h, shared/config.h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md y esta tanda. Sin SCM, instalacion ni GTA.
- Ampliar seat a -3 cambia el rango serializado: protocolo 0.4.20-alpha y recompilar testbot.
- Crear en autoridad tras heading final; conservar suelo, LOS y volumen, sin fallback ni copias en receptor.
- Hasta tres invitados: conductor y pasajeros por vehiculo; filtrar F1, muertos, desconexion y area en cada frame.
- Motos esperan al host hasta 90 s; luego 8 s para bajar al invitado y recibir el vehiculo; cancelar al terminar mision.
- TWAR7 del repo esta modificado: verificar ambos warps de OGLOC y no depender de sus numeros de linea originales.

Resultado T40: camino unico de creacion en autoridad y cola por frame; ID de red antes del envio, conductor -3 y pasajeros model=0 sin fallback en receptor. Drive-Thru tras 0175; TWAR7 motos 36@ tras 0175/272.5, una por pareja y espera al host (90 s + 8 s); auto 37@ tras warp de OGLOC/292 en ambos caminos de prision, heading estable antes de crear.
Conserva suelo/LOS/volumen y exclusion del enemigo en Drive-Thru; blip por CMirrorBlips y liberacion T37. ParkCopiesNextTo/INTRO1 conservados, conversion de radianes a grados en los emisores. Interruptor mission-og-loc-guest-car=true.
Referencias: gta-reversed TheScripts.h (LocalVariablesForCurrentMission 0xA48960), CharacterCommands.cpp WarpCharIntoCar/WarpCharIntoCarAsPassenger y World.cpp consultas de suelo/LOS/colision. TWAR7 archivado tiene ambos warps equivalentes en 285/696 (OGLOC slot 3), heading 184 antes del segundo en 667; el primer camino usa Skip.SetUpForSpecificVehicle 402. No se modifico SCM.
Validacion T40: client -r OK (39.406 s) y recompilacion final OK (25.11 s), server OK (7.11 s), testbot OK (2.344 s); CRLF y git diff --check OK. Protocolo 0.4.20-alpha (rango seat -3..7), BUILD instalado intacto. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin GTA ni ejecucion del testbot.

### T41 - critica previa y alcance (Codex)
Archivos reservados: client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, client/src/CConfigLoader.cpp y .h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA; resumen <=8 lineas.
- Limitar a mission boarding y mision 16; interruptor apagado y otras misiones conservan salto de lejanos.
- Con 3-4 jugadores reservar cada asiento asignado y conservar prioridad de cercanos; sin asiento no cambiar copias.
- Mantener reserva de NPC entrando, segun gta-reversed CarEnterExit.cpp ComputePassengerIndexFromCarDoor (0x64F1E0).
- F1 individual, abandono, muerte, desconexion, otra area y ya en vehiculo conservan filtros; no agregar pendientes.

Resultado T41: interruptor true con carga/guardado; solo mision 16 y boarding, asiento libre reservado y walk=false para lejanos. Cercanos, NPC entrando, filtros y copias intactos. Client OK (26.688 s), server -r OK (9.328 s), CRLF C++ y diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni GTA.

### T42 - critica previa y alcance (Codex)
Archivos reservados: client/src/Hooks/PlayerHooks.cpp, client/src/CBulletImpactForceGuard.h, TESTEAR_HOY.md, ESTADO.md y esta tanda. Sin protocolo, instalacion ni GTA.
- Revisar conductor y todos los pasajeros de un vehiculo valido; con 3-4 jugadores basta un ocupante de red.
- F1 individual conserva proteccion fisica; no depende de participantes de mision ni agrega pendientes por muerte/desconexion/interior/distancia.
- Autos vacios y solo NPC conservan impacto local nativo; interruptor apagado omite la inspeccion y restauracion.
- Mantener dano, gomas y efectos nativos; umbral 0.002 solo cambia log, conservando limite compartido de 2 s y limitacion de explosiones de T34.

Resultado T42: guard local extendido a conductor o cualquier pasajero de red, con validacion de pool/vtable antes de inspeccionar; propio auto intacto. Umbral de log 0.002 y limite de 2 s. Referencia gta-reversed Weapon.cpp:609-622, ApplyForce nativo; dano sin cambios.
Client -r OK (36.922 s), server -r OK (7.407 s); CRLF C++ y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo ni GTA.

### T43 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, client/src/CConfigLoader.cpp y .h, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA.
- 3-4 jugadores toman solo el host conocido; no bajar progreso propio, ni tocarlo durante mision, muerte, arresto o escena; F1 individual tambien necesita el lanzador.
- Buscar firma unica 03A4/09/SWEET de 8 bytes dentro de MainScriptSize; fallo sin reintentos hasta nueva sesion (reinicio del contador de frames).
- SWEET no crea blips; el LOCATE dibuja el marcador nativo y 03EE filtra antes de escribir onmission. Conservar lock y autoridad coop; no copiar otros globales.
- Desconexion no reinicia el intento en la misma partida; validar pIdleScripts antes de StartNewScript (gta-reversed TheScripts.cpp:1029 no valida pool vacio). Apagado conserva comportamiento anterior.

Resultado T43: firma original 03A4/09/SWEET unica verificada en offset 62090 (no hardcodeado); hilo ausente creado con StartNewScript y pool libre validado, una vez por partida. Contador nunca baja; solo fuera de mision/escena y jugador PLAYING. SWEET no crea blips; lock, autoridad y $onmission conservados, mismatch omitido solo en originales.
Validacion T43: client OK (17.984 s), server -r OK (7.141 s); CRLF C++ y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni escrituras en GTA.

## Resultado de la prueba de la tercera vuelta
T39, T40, T41 y T42 probados OK por el dueño. T43: el log del invitado dio `label-unavailable matches=0`: `ScriptSpace` es un puntero en el SDK, `sizeof` daba 4 y la búsqueda nunca corría (el nombre en el `main.scm` sí está en mayúsculas, offset 62090). Además `started` podía quedar puesto al cargar otra partida. Corregido a mano en 2026-11-09.5.

## Cuarta vuelta (2026-11-09)
Dueño de esta vuelta y del protocolo: Claude.

| Tarea | Pedido | Estado |
|---|---|---|
| T44 | Lanzadores de Ryder, Smoke y OG Loc para invitados | probado OK por el dueño (build 2026-11-09.7) |
| T45 | Ver a quien juega Cesar Vialpando | NO implementado: el dueño decidió dejarlo así |

### T44 - critica previa y alcance (Codex)
Archivos reservados: client/src/CMissionSyncRules.cpp, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md y esta tanda. Sin protocolo, SCM, instalacion ni GTA; resumen <=8 lineas.
- Con 3-4 jugadores usar solo el host conocido; CHAIN_UNKNOWN en progreso o requisito omite esa fila, sin bloquear las otras.
- F1 individual conserva lanzadores; muerte, arresto, escena y $onmission bloquean todo; desconexion e interruptor apagado conservan comportamiento previo.
- No bajar contadores ni duplicar hilos: firma unica y espera de 5 s independientes por fila, pool libre validado; interior/distancia sin colas nuevas.
- INITIAL inicializa coordenadas; horarios de RYDER/STRAP y filtro 03EE (lock, bAlone, autoridad) siguen nativos; no copiar flags ni crear blips.

Resultado T44: tabla SWEET/RYDER/SMOKE/STRAP; progreso y habilitacion del host, CHAIN_UNKNOWN omite fila; contador nunca baja, firma unica y espera de 5 s por fila. SWEET conserva su salida al completar cadena.
Original main.scm (3079599 bytes): firmas unicas 62090/63143/63492/63835, globales 1808/1812/1816/1820 y finales 9/3/4/5 verificados. INITIAL.txt:970-981 inicializa coordenadas; SWEET4:4012, SWEET2:3428 y TWAR7:3359 habilitan cadenas; RYDER/STRAP conservan horarios, sin otros flags impeditivos encontrados ni blips en los lanzadores.
FilterMissionStart intacto: IsLaunchBlocked, bAlone y autoridad; $onmission comun. Referencia gta-reversed TheScripts.cpp:1029 (StartNewScript sin guard de pool, conservado en caller).
Validacion T44: client -r OK (43.078 s), server -r OK (7.907 s); CRLF y git diff --check OK. SIN REVISAR / SIN PROBAR / SIN INSTALAR; sin protocolo, SCM ni escrituras en GTA.

### T45 - critica previa y alcance (Codex)
Archivos reservados: esta tanda, TESTEAR_HOY.md, ESTADO.md y docs/MISIONES_MODO.md. Solo documentacion; sin C++, protocolo, SCM, interruptor, instalacion ni GTA. Resumen <=8 lineas.
- Con 3-4 jugadores y F1 individual, identificar entidades por mision y propietario: createdBy y creator del ID no distinguen SWEET6 de otros scripts del mismo jugador.
- Conservar autoridad del jugador que corre SWEET6, incluso si es invitado: bloquear toma por host, conductor espectador y claims; la distancia sola no cubre esos caminos.
- Proteger tambien en la simulacion del dueño: invulnerabilidad/no robo solo en la copia espectadora no detiene disparos replicados ni colisiones del jugador remoto.
- Muerte, desconexion, fin, liberacion del script e interior/distancia requieren retirar proteccion sin transferir actores aun usados; apagado debe conservar exactamente el flujo previo. LOWR queda fuera.

Resultado T45: NO IMPLEMENTADO, segun la salida de seguridad expresa de la tarea. No basta quitar SWEET6 de IsLocalOnlyMissionRunning; no se afirma imposibilidad general, sino ausencia de garantias en el flujo actual.

1. Visibilidad actual (lectura estatica, sin juego): WorldHooks.cpp:122 excluye solo peds MISSION_CHAR mientras corre SWEET6/LOWR no sincronizado (COpCodeSync.cpp:661). Una mision F1 individual fuera de esa lista crea peds de red y envia tareas/estado en CNetworkPedManager::Update; individual no equivale a entidades aisladas. Los VEHICULOS de SWEET6 ya pasan por WorldHooks -> CNetworkVehicle::CreateHosted -> VehicleSpawn, sin ese filtro. CNetworkVehicleManager.cpp:152 y PacketHandlers/vehicles.cpp:122 solo quitan colision de copias remotas solapadas cuando el receptor corre su propia mision local-only (4/6 m de histeresis); un espectador sin esa mision no activa ese ghost. Esto no garantiza baile/hidraulicos ni apariencia completa.
2. Autoridad: CHandover.cpp:192/271 excluye MISSION_CHAR/MISSION_VEHICLE del traspaso por distancia, pero procesa claims de peds antes del filtro; MoveNearbyPedsTo tampoco filtra MISSION_CHAR. PacketHandlers/peds.cpp:414 permite PED_TAKE_HOST al host sin reserva por SWEET6: si juega un invitado, el host podria tomar su ped. PacketHandlers/vehicles.cpp:120-146 acepta conductor con asiento libre y ReassignSyncer sin excluir MISSION_VEHICLE; entrar como pasajero si conserva el filtro de mision (:315). Los managers cliente no establecen una reserva exclusiva por script; perder syncer separa tareas locales y actualizaciones de red. La eliminacion habitual de entidades invalida referencias y elimina registros; no hay ciclo de reserva/proteccion SWEET6 que liberar al terminar.
3. Interferencia: VEHICLE_DAMAGE (server PacketHandlers/vehicles.cpp:226) guarda y reenvia dano de cualquier emisor, sin exigir syncer. PacketHandlers/players.cpp ejecuta DoBulletImpact remoto sobre la entidad traducida tambien en el dueño; CBulletImpactForceGuard solo restaura velocidades, no salud ni gomas. DamageHooks::ShouldDiscardAllyEvent excluye misiones individuales y Cesar: no es proteccion para este caso. Chocar sigue usando fisica nativa; robar/conducir permite el cambio de syncer anterior. Una copia espectadora invulnerable o cerrada por si sola no protege al dueño de todos esos caminos.

Referencia gta-reversed: source/game_sa/Events/EventDamage.cpp:78, AffectsPed tiene excepciones para bInvulnerable (jugador local, explosion y ahogo); no basta activar ese bit. source/game_sa/Entity/Physical.cpp:981, ApplyCollision aplica fuerzas y calcula intensidad de dano independientemente de la sincronizacion. No se copio codigo ni se parcheo el exe.
Siguiente alcance necesario: disenar identidad y ciclo de vida por entidad SWEET6 (incluyendo entidades preexistentes usadas por el script), reserva de syncer validada en servidor y proteccion de golpes/disparos/explosiones/colisiones/entrada tambien en el dueño; sincronizar esa identidad con espectadores tardios y limpiar al terminar/desconectar. PedSpawn/VehicleSpawn solo llevan createdBy, modelo, posicion e ID de creador, no pertenencia a SWEET6; si ese diseño exige protocolo, detenerse y explicarlo antes de implementarlo. No se agrego mission-individual-spectate porque no existe implementacion segura que conmutar. LOWR intacto; baile y salto de lowriders no se replican en T45.

Validacion T45: xmake build -r client OK (38.672 s), xmake build -r server OK (10.594 s); git diff --check OK. Solo cuatro documentos modificados, C++/CRLF intactos. SIN REVISAR / NO IMPLEMENTADO / SIN INSTALAR; sin ejecutar el juego.
