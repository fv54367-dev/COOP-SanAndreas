# T77 - Animacion nativa del ladron
Objetivo: reproducir apertura y arrastre en victima y terceros desde PLAYER_JACKED sin adelantar la liberacion del asiento.
Archivos: client/src/CJackSync.cpp, client/src/CJackSync.h, client/src/PacketHandlers/vehicles.cpp y esta tanda.
Documentacion: TESTEAR_HOY.md y ESTADO.md reservados por T78 (#48); coordinar antes de editar o dejar texto aqui.
Restricciones: CRLF; no modificar CreateJackedVictimTask ni StartNativeDraggedOut; sin protocolo, SCM ni instalacion. Resumen <=10 lineas.
Aceptacion: client/server/testbot compilan; citas gta-reversed; revision cruzada y juego pendientes.
Casos borde:
- 3-4 jugadores: reproducir solo con ambos peds y vehiculo presentes, misma area y distancia acotada.
- F1 individual/mision coop: no cambia autoridad ni reglas de mision.
- Muerte/desconexion/borrado: referencias nativas, registro solo IDs, limite temporal y limpieza.
- Cancelar antes de abrir: sin PLAYER_JACKED hasta arrastre real.
- Moto/BMX: EnterCar nativo elige BikeJacked; sin animaciones manuales de puerta.
- player-jack-native=false conserva camino anterior; lejos/interior diferente usa fallback.
- Asiento delantero: ComputeSlowJackedPed selecciona por puerta, no por rol asDriver.

## Cambio y evidencia
- PLAYER_JACKED inicia EnterCar nativo en el ladron remoto y deja que ese EnterCar cree la tarea de la victima al llegar al arrastre. VEHICLE_ENTER posterior no lo reemplaza.
- IsPlayerJackInProgress protege ambos participantes frente a ONFOOT, keys, tasks y warps usando las guardas T72 existentes. PLAYER_BEING_JACKED no reinicia una victima ya protegida.
- Registro solo de IDs; referencias a entidades a cargo de las tareas nativas. Cancelacion por muerte/desconexion/cambio de area, tarea terminada o 8 s. Fallback existente para victima local si la secuencia termina/falla sin sacarla y el ladron sigue presente y vivo.
- gta-reversed source/game_sa/Tasks/TaskTypes/TaskComplexEnterCar.cpp:211, 424-511, 801: seleccion por puerta, ramas car/bike y tareas emparejadas. source/game_sa/CarEnterExit.cpp:172 ComputeSlowJackedPed (0x64F070), :432 IsCarSlowJackRequired (0x64EF70). Se llama al juego, sin copiar implementacion ni agregar hooks.
- SDK ComputeSlowJackedPed declara int aunque devuelve CPed*: conversion explicita. Puerta SDK 0 normalizada a puerta nativa 10.
- Limitacion: el replay comienza al recibir el arrastre, no en la aproximacion inicial; no promete sincronizacion de frames. Si falta entidad, area no coincide o distancia >12 m, fallback anterior.
- Caso 2 (F junto al acompanante delantero) queda pendiente, autorizado por la tarea: el juego selecciona pasajero 0 por puerta 8 aun con asDriver=true. Process de T72 reporta seat=-1 por asDriver y NotifyRemoteJacked usa ese asiento tambien para destino del ladron. Corregir solo la victima cambiaria erroneamente el destino a pasajero; hace falta separar esas dos semanticas antes de tocarlo.
- CreateJackedVictimTask y StartNativeDraggedOut intactas, comparadas contra HEAD. Sin protocolo, SCM, instalacion ni ejecucion de juego.

## Documentos compartidos pendientes por reserva #48 de T78
No se editaron TESTEAR_HOY.md ni ESTADO.md: tablero sigue reservandolos. Texto listo para integrar por el orquestador:

TESTEAR_HOY.md:
T77, SIN REVISAR / SIN PROBAR / SIN INSTALAR. Con player-jack-native=true, A conduce y B lo roba: en A y en un tercer/cuarto cliente B debe ir a la puerta, abrir y sacar a A; A debe completar caida y levantarse sin teleport. Repetir intercambiando roles, con pasajero por G, moto y BMX. Cancelar antes de abrir no debe expulsar a nadie. Desconectar/matar ladron o victima durante secuencia, probar F1 individual, mision coop, otra area y distancia. Apagar interruptor conserva camino anterior. Buscar player-jacked thief-replay en mirror.log. F en puerta del acompanante delantero sigue pendiente (caso 2); no darlo por arreglado.

ESTADO.md, Pendiente (revision y prueba):
T77 animacion del ladron: EnterCar nativo empareja ladron/victima en clientes receptores y las guardas T72 protegen ambos. Registro por IDs y limite 8 s, fallback local. Referencias gta-reversed TaskComplexEnterCar.cpp:211/424-511/801 y CarEnterExit.cpp:172/432. Caso 2 acompanante con F pendiente por mezcla entre asiento victima y destino ladron. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

## Validacion
- xmake build -r client OK (33.407 s), despues de todos los cambios.
- xmake build -r server OK (7.328 s), xmake build -r testbot OK (2.438 s).
- git diff --check OK; CRLF verificado; funciones protegidas intactas.
- Testbot compilado, suite no ejecutada: no hay cambios de server/protocolo y no verifica animaciones.
- Sin commit: .git de solo lectura en esta sesion. Reserva #50 mantenida hasta commit por el orquestador.
