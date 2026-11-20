# Tanda 2026-11-10 sync visual de NPC

Sigue a `TANDA_2026-11-09_sync_dano.md` (daño confirmado en juego el 2026-11-10).

Decisión del dueño (2026-11-10): la copia que tarda 1 o 2 segundos en caer se acepta como está. No se retoma.

## T47: `npc-aim-sync`, la copia apunta y dispara (Claude a mano)
Síntoma: el invitado ve quieto al NPC que le dispara; solo levanta el arma al recargar y se ve el fogonazo.

Causa (gta-reversed `TaskSimpleUseGun.cpp`, `ControlGun` 0x61E040 y `ProcessPed` 0x62A380): la tarea de arma solo sigue viva si alguien le da una orden **cada frame** (`m_IsInControl`); en el dueño lo hace la IA del NPC. En la copia, `PED_ONFOOT` creaba la tarea y cada 50 ms le cambiaba el objetivo, sin darle órdenes: la tarea se terminaba sola y se volvía a crear. Y el disparo (`PED_SHOT_SYNC`) llamaba a `CWeapon::Fire` directo, sin pasar por la tarea, así que no había animación de disparo.

Cambio: la copia recuerda si su dueño apunta y a dónde. `CNetworkPedManager::Process` (cada frame) le da a la tarea la orden de apuntar, y la de disparar cuando llegó un `PED_SHOT_SYNC`. La bala de la tarea no sale (`CWeapon__Fire_Hook` ya la corta en las copias); la traza y el efecto siguen saliendo del paquete.

Archivos: `client/src/CNetworkPed.{h,cpp}`, `client/src/CNetworkPedManager.cpp`, `client/src/PacketHandlers/peds.cpp`, `client/src/CConfigLoader.{h,cpp}`, `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md`. Sin protocolo nuevo (sigue 0.4.21-alpha).

Casos borde:
- 3-4 jugadores: cada copia se anima sola con los mismos paquetes.
- NPC en vehículo (drive-by), muerto o subiendo a un auto: no se toca, queda como hoy.
- Dejan de llegar paquetes (lejos, fuera de rango): sin ped no hay nada que animar; al volver, el primer `PED_ONFOOT` corrige.
- Disparo que llega sin que la copia estuviera apuntando: apunta 400 ms para mostrar el tiro.
- Daño: no cambia; lo decide el dueño (T46).
- Interruptor `npc-aim-sync = false`: código anterior.

Aceptación: compila; en juego, prueba en `TESTEAR_HOY.md`.

## Estado
Compilado e instalado el 2026-11-10. SIN PROBAR en juego. Falta la revisión cruzada de Codex.

## T47 confirmado (2026-11-10)
El dueño confirmó las tres pruebas. Pedido nuevo, sin pedir aprobación y sin abrir los GTA (solo instalar): que los NPC se vean lo más parecido posible entre los GTA (pelea cuerpo a cuerpo, pasos adelante y atrás, deslizamiento), que no se dupliquen policías, peatones, bomberos ni ambulancias, y que el robo de vehículos entre jugadores se vea fluido.

**Protocolo: 0.4.22-alpha** (`PED_ANIM_SET`). GLM trabaja a la vez en trenes en este mismo árbol (`TANDA_2026-11-10_glm_sync_trenes.md`): le queda 0.4.23, nota de coordinación en su tanda. T48 es de la tanda de cinemáticas; acá se sigue con T49.

## T49: `npc-anim-mirror`, la copia reproduce el mismo conjunto de animaciones
Causa: la copia solo sabía "camina" o "está quieto" (move state) más **una** animación de una lista blanca, mandada como mucho una vez por segundo (`TASK_SYNC_SEND_INTERVAL`). La pose de pelea y sus pasos (FIGHTIDLE, FIGHTSH_*) no estaban en la lista: la copia caminaba en su ciclo normal mientras la posición la movía hacia atrás, y por eso se deslizaba; los golpes de menos de un segundo se perdían.
Cambio: el dueño manda cada 100 ms las 6 animaciones más fuertes del NPC a pie tal como están (grupo, id, mezcla, tiempo, velocidad) si hay otro jugador a menos de 70 m. La copia deja ese mismo conjunto: agrega las que faltan (`CAnimManager::AddAnimation`), les pone mezcla, velocidad y tiempo, y apaga el resto como hace `BlendAnimation`. Los bloques que no estén cargados (estilos de pelea) se piden y se retienen mientras exista la copia. Las animaciones con nombre siguen por `npc-named-anim-sync`.
No se aplica a copias muertas, sentadas en un vehículo, subiendo o bajando, ni mientras las sacan de un auto.
Archivos: `shared/network/packets/peds.h`, `packet_types.h`, `config.h`, `client/src/CNetworkPed.{h,cpp}`, `CNetworkPedManager.cpp`, `PacketHandlers/peds.cpp`, `server/src/PacketHandlers/peds.cpp`.
Casos borde: 3-4 jugadores, cada copia aplica el mismo paquete; dueño que cambia, el nuevo empieza a mandar y el viejo deja (el server descarta al que no es dueño); copia lejos de todos, deja de recibir y sigue con lo que tenía (la animación base la repone `EnsureBaseAnimation`); `npc-anim-mirror=false` en el que recibe, se ve como antes (el que manda sigue mandando).

## T50: `emergency-dedupe` y `spawn-watch`
- **Dos ambulancias, dos camiones de bomberos.** Causa (gta-reversed `CarCtrl.cpp:441`): `CCarCtrl::GenerateEmergencyServicesCar` (0x42F9C0) corre en cada juego, y cada juego ve el mismo accidente o incendio y manda su vehículo (`GenerateOneEmergencyServicesCar` 0x42B7D0, sitios 0x42FAE6 y 0x42FB89). Ahora lo manda solo el jugador dueño de ese punto (`CStreamingSync::IsPointMine`, el mismo reparto del tráfico), y nadie manda otro si ya hay uno del mismo modelo de otro jugador a menos de 150 m. Log `emergency-skip`.
- **Policías y peatones duplicados: sin causa encontrada leyendo el código.** El reparto por punto ya cubre tráfico, peatones y atractores, y la persecución es por jugador. En vez de adivinar, `spawn-watch` deja evidencia en `CoopAndreas_mirror.log` cada 5 s: `dup-ped` (dos peds del mismo modelo a menos de 1,2 m, con quién sincroniza cada uno y su `createdBy`), `dup-vehicle` (dos vehículos de emergencia del mismo modelo, uno propio y uno ajeno, a menos de 60 m) y `cop-watch` (policías propios, copias y locales, con las estrellas). Con eso se arregla en la tanda siguiente.
Archivos: `client/src/CSpawnWatch.{h,cpp}` (nuevo), `Hooks/CHook.cpp`, `Main.cpp`.

## T51: `player-jack-native`, robo de vehículo entre jugadores
Causa: a la víctima se le ponía una animación suelta en el asiento (`CTaskSimpleRunAnim` CAR_JACKED) y a los 900 ms se la colocaba afuera de golpe (`CTaskSimpleCarSetPedOut`). Con los NPC se usa la tarea del juego. Ahora la víctima (en su PC y en la copia que ven los demás) recibe la misma tarea que el juego le da a cualquiera que sacan de un auto (`CTaskComplexCarSlowBeDraggedOutAndStandUp`, 0x648620; gta-reversed `TaskComplexEnterCar.cpp` `CreateDragPedOutSubTask`): la sacan por la puerta, cae y se levanta. Si a los 5 s sigue adentro, corre la salida forzada de antes.
Archivos: `client/src/CJackSync.cpp`.
Casos borde: víctima muerta o sin vehículo, no se toca; paquete repetido, se ignora (ya había); la víctima no manda su propio aviso de "me están sacando" mientras dura; `player-jack-native=false`, como antes.

## Estado (2026-11-10)
T49, T50 y T51 compilados (client, server, testbot; testbot TODO PASS) e instalados. **SIN PROBAR en juego**; los GTA no se abrieron (pedido del dueño). Falta la revisión cruzada de Codex.

## Revisión cruzada de T47 y T49-T51 (Codex local, solo lectura) y T52
Codex encontró tres cosas en `9c85f1a`, las tres aplicadas:
- **T51:** `AddTaskPrimaryMaybeInGroup` solo encola un evento, y la tarea lee el vehículo del ped sin chequear al arrancar (`CreateFirstSubTask` 0x648A10): si el vehículo desaparecía en esa ventana, crash. La víctima jugador recibe la tarea con `SetTask` directo.
- **T49:** el bloque de animación se pedía con `GAME_REQUIRED` y quedaba retenido para siempre; ahora es un pedido simple y lo sostienen las referencias de la copia.
- **T49:** una animación con velocidad 0 seguía corriendo en la copia; la velocidad se asigna siempre.
T47 (`ProcessAim`) y el gancho de emergencias: sin hallazgos.

**T52, arreglos de la auditoría de GLM** (`AUDIT_2026-11-10_sync_glm_informe.md`, hallazgos 1, 2 y 6, verificados en el código antes de aplicar):
- NPC pasajero: `PedPassengerSync` salía con asiento 0 cuando el ped no estaba en el arreglo de pasajeros (cambio de asiento) y la copia saltaba al asiento delantero. Ahora no se manda sin asiento, y el receptor no hace warp a un asiento que el modelo no tiene.
- `GetCameraResyncRate` comparaba la X vieja del mouse con la Y nueva: moviendo el mouse en un solo eje mandaba la cámara en cada frame.
Hallazgos 3, 4 y 5 de esa auditoría: sin hacer.

## T72: el sync interrumpía la salida nativa (2026-11-10, Codex)

**SIN REVISAR / SIN PROBAR / SIN INSTALAR.** Tanda de archivos/reserva:
TANDA_2026-11-10_codex_t72.md. Sin SCM, protocolo ni cambios del servidor.

Evidencia del código y recorrido (no es confirmación visual del arreglo):
- Antes, Hooks/TaskHooks.cpp:26 mandaba PLAYER_JACKED desde el constructor de entrada,
  antes de caminar/abrir la puerta; el pasajero hacía lo mismo en CJackSync::Process.
  La víctima local iniciaba su tarea y su reloj de 5 s inmediatamente (CJackSync.cpp:516).
  Podía salir o llegar al fallback antes de que el ladrón la tocara, incluso si cancelaba.
  El server libera el asiento al recibir PLAYER_JACKED (server/src/PacketHandlers/vehicles.cpp:385),
  reenvía a todos, incluido ladrón; no mueve ningún ped del juego.
- En la PC del ladrón el juego arrastra a la copia remota; en la de la víctima
  ApplyLocalJacked inicia su propia tarea. En un tercer cliente PlayRemoteJacked
  inicia la tarea de su copia. Los tres relojes pueden terminar en distintos frames.
  PLAYER_ONFOOT_UPDATE hacía RemoveFromVehicle sin mirar la tarea
  (client/src/PacketHandlers/players.cpp:68), y CNetworkPlayer::RemoveFromVehicle
  (client/src/CNetworkPlayer.cpp:389) borra PRIMARY y ejecuta CarSetPedOut en modo warp.
  Después ONFOOT aplica SetPosn; también VEHICLE_EXIT forzado ejecutaba CarSetPedOut.
  La supresión de T51 solo cubría reentrar por DRIVER/PASSENGER, no esas salidas/posiciones.
- El pasajero local seguía enviando PASSENGER_UPDATE mientras lo sacaban.
  Si el ladrón ya ocupaba el asiento, el server contestaba SEAT_DENIED, cuyo handler
  hace WARP_CHAR_FROM_CAR_TO_COORD a 3 m del auto (PacketHandlers/vehicles.cpp:530).
  Ahora se protege ese handler y se deja de reclamar el asiento durante el arrastre.
- gta-reversed Tasks/TaskTypes/TaskComplexEnterCar.cpp:801
  CreateDragPedOutSubTask entrega CarSlowBeDraggedOutAndStandUp a la víctima;
  TaskSimpleCarSlowBeDraggedOut::SetPedPosition (0x64C0E0) alinea con la puerta y
  ProcessPed (0x64E180) espera animación/caída antes de salir. RemoveFromVehicle/SetPosn
  compiten con ese dueño nativo de posición. El exe local confirma CreateFirstSubTask
  en 0x648A10 y la tarea lenta 0x337; no se agregó ningún hook/dirección de parche.
- Moto/bici: TaskComplexEnterCar.cpp:487-511 usa TaskSimpleBikeJacked (0x648B90),
  no la tarea de puertas del auto. Se construye esa tarea con sus referencias nativas,
  sin guardar punteros de red; se restaura CantBeKnockedOffBike=2 para permitir la caída.

Cambio:
- PLAYER_JACKED sale al detectar m_pDraggedPed con tarea de arrastre real.
  Luego sale VEHICLE_ENTER por el mismo canal EVENT fiable: primero liberar víctima,
  después reclamar asiento del ladrón. Los constructores no avisan un robo cancelable.
- IsPlayerJackInProgress consulta la tarea viva, incluso quick/bike y levantarse;
  protege ONFOOT, EXIT, KEY_SYNC, SET_PLAYER_TASK, smoothing y correcciones de asiento.
  Health/armour siguen actualizándose y muerte/desconexión mantienen su limpieza normal.
- No se modifican reglas de misión/F1. Con false no actúan los guards ni el aviso diferido:
  queda el camino anterior a T51. El timeout local de recuperación de T51 sigue en 5 s.

Casos borde/prueba: conductor/acompañante, ambos sentidos, 3–4 observadores,
cancelación durante aproximación, misión coop sin muerte, F1 individual, otra área,
desconexión de víctima, muerte de ladrón, moto/bici y opción apagada (TESTEAR_HOY).
Un paquete de arrastre que llegue después de que la víctima ya no exista/siga sentada
se descarta sin recrearla ni teletransportarla; no se promete sincronía exacta de frames.

Verificación T72: cliente compiló tras todos los cambios; server/testbot reconstruidos
con -r OK. Runner canónico con solo SRV1/SRV2 reubicados dentro de build por el
sandbox: 90 + 3 chequeos, cero fallas, TODO PASS (build/t72_regression.log).
No verifica animación en juego. CRLF/diff --check OK. Sin commit (.git solo lectura).
