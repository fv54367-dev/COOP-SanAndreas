# Revisión cruzada 2026-11-14 (Claude: 3 Sonnet + 2 Opus de solo lectura, verificado por Claude)

Alcance: todo lo de `probada-2026-11-13..9e88307` (misiones, NPC/red, mod-check, VS, billar, launcher) y lo viejo con "rev falta" (Drive-By, Los Sepulcros, Running Dog, César, Life's a Beach, Home Invasion, autos de invitados, cinemáticas, NPC, trenes, voz, UPnP, desafíos).

## Arreglado (commits `cd6129f`..`6886a3b`, compilado, sin probar)
- mod-check: un manifiesto repetido ya no se compara de nuevo (spam en el log y el chat del host).
- `ACTUALIZAR.bat`: el ini del Open Limit Adjuster se escribe en ANSI (no rompe tildes) y un ini vacío no corta la actualización.
- Aviso de Open Limit Adjuster: "reiniciá" solo si se pudo escribir el ini; `0 ; comentario` cuenta como 0.
- Server: `PED_ONFOOT` con NaN se descarta (si no, `handover-proximity` le daba el NPC a cualquiera).
- `npc-driveby-sync` ya no depende de `npc-aim-sync`.
- Armas de pickups (`mission-pickup-weapon-share`): el respaldo "el host ya tiene el arma" exige que el host esté a menos de 4 m del pickup (antes un pickup barrido al terminar la misión armaba a todos).
- Artillero del invitado (`coop-car-gunners`): un corte de sync de menos de 0,75 s no lo suelta; si el host cambia de auto, el invitado cambia también.
- Autos copia (`PUT_IN_VEHICLE_SCRIPTED`): el modelo que llega por red se valida (solo modelos de vehículo) y se libera después de crear el auto, como hacen los scripts.
- Armas recibidas (`MISSION_PLAYER_EQUIPMENT`) y trenes que no cargan: el modelo ya no queda fijo en memoria.
- `interior-population-sync`: el empleado adoptado (peluquero, cajero) ya no se borra con `delete` al irse la copia; sigue siendo del script del negocio.
- César: el lowrider del invitado aguanta 8 s sin paquetes del host (antes 2 s: una pausa lo borraba en pleno concurso).
- Baile (modo no equipo): después de un paquete perdido el puntaje se resincroniza en vez de congelarse.
- Home Invasion: el invitado mide solo su propio ruido (con -1 contaba también el del host y los demás: la barra subía varias veces más rápido).

## Encontrado y NO arreglado (decidir / falta evidencia)
- **`rule-guest-car-miss`** (el invitado maneja el auto de la misión y no avanza), causas posibles en orden:
  a) el invitado maneja una **copia** igual al auto de la misión (mismo modelo y color, estacionada a 6-9 m): el script pregunta por otro auto. Se ve como `rule-guest-car-near` en el log del host.
  b) la misión pregunta con otros opcodes (`IS_CHAR_IN_MODEL` 0x00DD, `IS_CHAR_IN_ANY_CAR` 0x00DF, `STORE_CAR_CHAR_IS_IN` 0x03C0, `GET_CAR_CHAR_IS_USING` 0x0811) que la regla no cubre.
  c) el script está en `requiresHostCar` (RYDER2, SMOKE2, SYN*, GUNS1, CAT2, DRUGS3, DRIV*...): ahí sale `-miss host-car=1`.
  Además, con el auto del invitado contando, un marcador de 20 m o menos espera a que el host esté a 12 m (`CMissionSyncRules.cpp` ~1332 y ~1395): es la regla "las misiones esperan a todos". El interruptor `mission-guest-car-host-reach` se lee pero no se usa.
- Running Dog / Wrong Side / Just Business: `ParkSharedGuestCar` cuenta invitados para asientos libres del host y no les deja auto; si el host arranca antes de que suban, quedan a pie (`mission-no-auto-board` corta el subir automático).
- Si el host se sube a un auto estacionado para los invitados, queda como "ya no necesario" y la población lo puede borrar.
- `fade-guard` devuelve la imagen al invitado a los 4 s aunque la pantalla del host siga negra.
- Lowrider y baile siguen mandando paquetes de tiempo hasta que termina el script (poco tráfico).
- Reglas de escena SWEET4A / SWEET7A / SMOKE2 sin el chequeo `vanilla-scripts` (`CScenePartner.cpp` ~3050, `CCutscenePartners.cpp` ~518).
- `FollowHostIntro` adivina el progreso del intro del host por los íconos: con el host dentro de INTRO1/INTRO2 podría saltar al invitado.
- Reloj de misión: si el script lo congela mientras lo tenemos frenado nosotros, al soltarlo se descongela el del script.
- `SET_INT_STAT` de LA1FIN2 se copia para todos los stats que toque ese script, no solo ciudades y Badlands.
- Voz: el mezclador va a ~25-31 ms por bloque de 20 ms y la cola descarta audio (voz cortada). Arreglo: `lastMix += 20`.
- UPnP: solo mapea si el router contesta error 714; routers que contestan 713/402/501 o que piden lease permanente (725) nunca mapean.
- `CCutscenePartners::Render` crea y borra autos y peds dentro del hook de render (0x53E9F9).
- `CCutsceneHidden::HideExtras` recorre todos los pools en cada frame de cinemática (solo rendimiento).
- `CSceneMocap` deja un hilo suelto que puede seguir al cerrar el juego.
- `VehicleHooks.cpp`: las 7 llamadas a `UpdateCarAI` se redirigen dos veces; el primer gancho (que copiaba también el volante) no corre nunca.

## Memoria que nunca se libera (sospechosos del bug de FPS)
Un modelo con GAME_REQUIRED / MISSION_REQUIRED / KEEP_IN_MEMORY no entra en la lista de lo que el streaming puede liberar. Arreglados: autos copia, armas recibidas, trenes. Quedan:
- `CScenePartner.cpp` ~102 y ~2585: bloques de animación de cada cinemática (COP_AMBIENT, PLAYIDLES, SMOKING, DEALER, GANGS, MISC) y `CJ_CIGGY` con GAME_REQUIRED.
- `CNetworkPed.cpp` ~1308, ~1368, ~1517, ~1553: bloques de animación de NPC con GAME_REQUIRED.
- `CPlayerAnimMirror.cpp` ~443: bloques de animación con KEEP_IN_MEMORY (encaja con los 399 KEEP_IN_MEMORY del volcado).
- `CObjectSync.cpp` ~199: modelos de objetos de misión con MISSION_REQUIRED sin `SetMissionDoesntRequireModel`.
- `vehicles.cpp` ~599: piezas de tuning con GAME_REQUIRED (chico; no tocado porque la tienda vanilla las maneja distinto).
Cada uno tiene un tope (cantidad de modelos distintos), pero sumados achican el presupuesto de streaming. Para confirmar: sesión larga con `perf t=` y un volcado de qué modelos tienen los flags 0x2/0x4/0x8.
