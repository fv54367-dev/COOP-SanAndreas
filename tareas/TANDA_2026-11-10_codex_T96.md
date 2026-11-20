# T96 - Just Business (DRUGS1, 30)

## Reserva y restricciones
Archivos: client/src/CConfigLoader.h, client/src/CConfigLoader.cpp, client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, client/src/CScenePartner.cpp, client/src/CCutscenePartners.cpp; esta tanda, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md, docs/MISIONES_MODO.md. Solo repo; no git, SCM, protocolo, instalacion ni GTA. CRLF con Python newline=''. Resumen <=10 lineas.

## Critica previa
No copiar offsets ni locales del fork. Smoke conduce por grabaciones 250-286; CJ es pasajero/tirador. Nunca reemplazar sus tareas ni esperar al grupo en los LOCATE que disparan trafico, saltos o explosiones. Moto propia de invitado significa conduccion libre y disparo normal, no otro Smoke ni repetir su grabacion sobre la misma via. No ampliar radios tacticos del edificio. Interior 18 requiere incluir escenas por script tambien dentro del interior, no solo area 0. Equipo MicroUzi, bajas, HUD, textos, blips y transiciones ya tienen canales; no duplicarlos.

## Casos borde
- 2-4: vehiculo separado por invitado vivo a pie, misma area y <=120 m; conservar vehiculos ya usados, suelo/colision/linea de vista T40. Sin sitio seguro, no crear.
- F1 individual, incorporacion tardia y abandono quedan fuera por ShouldIncludeNetworkPlayer. Cola cancela por salida/muerte/cambio de area/desconexion.
- Muerte/arresto falla para todos por sistema general; abandono invitado solo lo saca a el, salida de autoridad termina todos.
- Interior/lejos: transiciones existentes hacia el dueno; nunca mover dueno hacia invitado. No esperar durante playback de moto.
- coop-just-business=false conserva comportamiento previo; dependencias existentes de T40, equipo, espejo y escenas.

## Aceptacion
Client/server/testbot compilan; regresion ENet aislada si falta correr_testbot.bat, CRLF, pruebas documentadas. SIN REVISAR por Claude / SIN PROBAR EN JUEGO.

## Recorrido completo y escenas (DRUGS1.txt, 8188 lineas)
- 1-238: introduccion SMOKE4A y salida en 466 con Smoke. SMOKE4A usa rutas por rango 0/1/2 existentes + CScenePartner/CSceneMocap (escuchar y caminar); sin clip/ruta, ocultar. T40 crea 466 por invitado cercano a pie al conducir el dueno; conserva vehiculos existentes y asientos de Smoke.
- 239-509: viaje/dialogos y llegada al edificio, Car.Locate3D con esfera en 1722.568/-1597.92: espera de convoy a 50 m. Reagrupacion de Smoke y mensajes siguen propios del dueno, espejados.
- 510-1009: escena por script, llegada, conversaciones y preparacion de enemigos. Extras fuera de plano por T86, sin ocupar posiciones de los actores; restauracion al terminar o saltar.
- 1010-1307: entrada al area 18, escena/animacion de charla, dar MicroUzi/120 y toma de control. Teletransporte/area, camara, blips y equipo existentes; T86 ahora incluye interior 18 en esta mision. LOCATE detenido 1.5 m sigue del actor de escena, sin esperar a invitados.
- 1308-1937: combate dentro: enemigos, tareas de Smoke y avance entre salas. Dano/bajas T46 y consultas de atacante T63; no sumar otro contador ni ampliar LOCATE tacticos 3/6.5/8.5 m sin esfera. Dueno conduce transicion de salida, no la dispara un invitado a solas.
- 1938-2443: salida al area 0 y combate exterior. Transicion existente lleva invitados; enemigos autoritativos, textos/blips/contador espejados. Sin compartir dinero/estrellas.
- 2444-2881: escena de moto: fuera de plano T86. En SET_CAR_HEADING de la 581 inicial se estaciona una 581 por invitado a pie/misma area <=120 m antes de que arranque playback; cola T40 espera al embarque del dueno. Guard de reentrada evita creacion recursiva por comandos auxiliares. No copiar proofs ni salud excepcional del prop. MicroUzi/99999 usa canal de equipo existente.
- 2882-4146: primer recorrido, trafico, enemigos, dialogos, dano a vehiculos y cambios de playback: no esperar al convoy en esos LOCATE. Invitados conducen/disparan normal en sus motos; Smoke conserva conductor, CJ pasajero y grabaciones. T63 ya consulta 051C con cada atacante participante y T46 las bajas.
- 4147-5142: salto/autobus/camion y autos adjuntos: camaras por script fuera de plano T86; no duplicar playback ni colisiones de los props. Warp 0430 del dueno a su 581 no teletransporta a invitados a pie ni los saca de sus motos. Adjuntos y poses usan sync existente.
- 5143-6487: canal/perseguidores, disparos y explosiones: autoridad nativa, T63/da?o existente; LOCATE de vehiculos sin esfera no pide esperar. No cambiar temporizadores ni resultados del dueno.
- 6488-7042: escena del salto del puente, regreso a conduccion/disparo y ultimos perseguidores: fuera de plano T86 durante widescreen, restaurar al salir, vehiculos de invitado independientes.
- 7043-7279: despedida final dirigida con Smoke: extras ocultos/restaurados, audio/textos/camara espejados y MicroUzi/80 por equipo existente. Resultado conserva condiciones del dueno.
- 7280-8188: tareas restantes de perseguidores, subrutinas de audio/combate, passed/failed y limpieza: ciclo general de mision y T40 libera copias/colas; fallo de participantes general, sin recompensas inventadas.

## Referencias y limites
gta-reversed World.cpp FindGroundZFor3DCoord (0x5696C0), TestSphereAgainstWorld (0x569E20), GetIsLineOfSightClear (0x56A490), Scripts/RunningScript.cpp UpdateCompareFlag (0x4859D0), Entity/Vehicle/Vehicle.cpp y Entity/Placeable.cpp (vehiculos/transformacion). CREATE_CAR/SET_CAR_HEADING se reutilizan de T40; VehicleCommands.cpp de esta referencia no implementa esos dos handlers, Entity/Ped/Ped.cpp GiveWeapon (0x5E6080). Solo llamadas auxiliares existentes T40; sin hooks/direcciones/tareas nativas nuevos. Escenas MIT ya atribuidas en NOTICE.md; no se porto codigo externo nuevo.
Motos libres: no hay conductor NPC extra ni copia de grabacion para invitados. Durante las camaras dirigidas deben conservar sus motos, y al recuperar control pueden estar detras de Smoke; calibrar distancias/saltos en juego. Una distribucion por etapa, sin reintento si no hay sitio seguro. Que la red pase no verifica cinematica, streaming del interior ni salto de motos en el exe.


## Resultado y verificacion
- Implementados interruptor cliente coop-just-business (true, lectura/escritura), autos/motos T40, cola antes de playback, proteccion ante warp 0430, convoy inicial 50 m y reglas de escena SMOKE4A/interior 18. Seis fuentes C++ con CRLF validado.
- xmake build client OK (28.75 s), recompilacion final -r client OK (36.047 s). xmake build server OK, -r server OK (9.047 s). xmake build testbot OK, -r testbot OK (3.453 s). Sin errores pendientes.
- tools/correr_testbot.bat no existe en este clon. Equivalente aislado build/t96-testbot, puerto 34996, 4 jugadores, UPnP/persistencia apagados: all con handover=1 = 12 escenarios/121 chequeos/0 fallas TODO PASS; reinicio handover=0, d = 1 escenario/3 chequeos/0 fallas TODO PASS. Logs testbot-all.log y testbot-d.log en esa carpeta, procesos propios cerrados.
- Revisados interruptor apagado, participantes/area/vida, condicion 051C existente T63, radios de llegada vs. recorrido y reentrada de comandos auxiliares. Sin git, protocolo, SCM, instalacion ni GTA.
- Documentados tanda, ESTADO, TESTEAR_HOY, INTERRUPTORES, SISTEMAS y fila de mision. SIN REVISAR por Claude / SIN PROBAR EN JUEGO; especialmente retraso de motos propias durante camaras dirigidas, saltos/adjuntos y streaming del interior. No se afirma mision arreglada por compilar ni por pasar ENet.
