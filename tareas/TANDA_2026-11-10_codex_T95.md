# T95 - Wrong Side of the Tracks (SMOKE3, 29)

## Reserva y restricciones
Archivos: client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, client/src/CScenePartner.cpp, client/src/CCutscenePartners.cpp; esta tanda, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md, docs/MISIONES_MODO.md. Solo repo, sin git/SCM/protocolo/instalacion/GTA; CRLF con Python newline=''. Resumen <=10 lineas.

## Critica previa
El texto de tarea invierte los roles: el original tiene CJ conductor y Smoke tirador. No reemplazar a Smoke ni sus tareas. El fork tiene Coop.* y advertencia de no soportada: no usar sus offsets/locales para vanilla. No ampliar todos los LOCATE del tren: 20/30/40/45/50 m regulan velocidad, trafico y guiones. Solo ampliar perdida rectangular 200/320 m y alejamiento del Vago a pie 50 m. No convertir persecucion en espera de todos. T46 aplica bajas, sin duplicar dano ni contadores. La sincronizacion existente T62/T76/T80 conserva trenes/carriages; prueba de adjuntos en juego pendiente.

## Recorrido y escenas (referencia SMOKE3.txt, 3162 lineas)
- 1-98: SMOKE3A en casa de Smoke: usar rutas por rango + CScenePartner/CSceneMocap existentes; sin ruta/clip coherente, ocultar. No inventar asientos.
- 99-375: streaming, tren estatico, 466 con Smoke pasajero y CJ conductor, dialogos de viaje y blip estacion. Copia T40 por invitado cercano a pie; conservar vehiculos ya usados. Entrada sigue siendo del dueno con Smoke; llegada espera convoy a 50 m.
- 376-1049: escena por script en estacion: salida del auto, paseo CJ/Smoke, conversacion Vagos, carreras al tren, animaciones TRAIN/GANGS y salto a vagones. Invitados fuera de plano por T86, restaurados al terminar. No ocupar lugares de actores/tareas del SCM.
- 1050-1435: final normal o salto recrea tren, adjunta cuatro Vagos, prepara Sanchez 468, reagrupa Smoke y devuelve control. Una Sanchez por invitado cercano a pie cuando conduce el dueno; T40 libera copias/embarques. No copiar proofs/10000 HP del prop al vehiculo nuevo (CREATE_CAR normal).
- 1436-1680: Smoke recibe Tec9 y dispara; invitados reciben Tec9/1000 por canal existente para drive-by. Mantener controles de velocidad/encuentros del tren del dueno; nunca esperar a todos durante persecucion.
- 1681-2016: obstaculos, desprendimiento y ataque de Vagos; T46 permite dano de invitados. Alejamiento 50 m del Vago a pie considera participantes vivos/misma area para no borrarlo ante ellos.
- 2017-2154: contador de muertos y retorno con Smoke: bajas autoritativas existentes; espejo de textos/blips, sin nuevas recompensas ni estrellas compartidas.
- 2155-2354: perdida 200/320 m considera cualquier participante vivo/misma area antes del NOT. Fin del recorrido del tren sigue fallo nativo. Escenas de fallo con Smoke/camara tren: fuera de plano T86.
- 2355-2653: audio de persecucion/retorno sigue de Smoke/dueno; todos reciben espejo. No repartir su grupo entre invitados.
- 2654-2866: llegada a casa (convoy a 50 m), dialogo final por script y caminata de despedida: extras ocultos T86, restaurados al terminar.
- 2867-3162: reagrupar Smoke, blips, audio, secuencias y limpieza: ciclo general existente; dueno sigue recogiendo a Smoke. Sin depender de locales/IP del fork.

## Casos borde
- 2-4: una copia por invitado a pie/misma area <=120 m, piso/colision/linea de vista T40; sin sitio seguro no crear. Con vehiculo conserva el suyo; puede bajarse para tomar otra moto del mapa.
- Individual F1, abandonos y jugadores tardios no cuentan por ShouldIncludeNetworkPlayer; desconexion cancela cola existente.
- Muerte/arresto: fallo general para todos; abandono invitado solo el invitado; salida dueno termina todos.
- Otro interior/lejos no evita perdida; no teletransportar al dueno hacia invitados. Escenas restauran visibilidad/referencias con T58/T86.
- coop-wrong-side-tracks=false mantiene comportamiento previo; dependencias de reglas/espejo/T40/equipo/escenas existentes.

## Aceptacion
Compilar client/server/testbot y regresion ENet aislada si no existe tools/correr_testbot.bat. CRLF, documentacion de pruebas y revision cruzada/juego pendientes.

## Resultado
- Implementado coop-wrong-side-tracks (cliente, true por defecto, lectura/escritura). 466/Sanchez por invitado cercano a pie con T40, Tec9/1000 en etapa moto, marcadores con minimo 50 m. Se conserva vehiculo existente y Smoke con dueno.
- 0202 sobre tren 200/320 m y 00F2 sobre NPC de mision 50 m consideran participantes vivos de misma area antes del NOT; no cambian velocidad/trafico ni el fallo al final de via. Sin protocolo/SCM, offsets, hooks o tareas nativas nuevas.
- SMOKE3A: rutas rank 0/1/2 ya presentes en SceneCompat/cutscene_ways.txt, CScenePartner/CSceneMocap; sin recursos, ocultar. Estacion (incluido salto), fallos y despedida por script: ocultar/restaurar con T86. Sin port externo nuevo; atribucion MIT existente en NOTICE.md.
- xmake build client OK tras corregir m_nVehicleSubClass a m_nVehicleSubType del SDK (recompilo/enlazo, 19.687 s). xmake build server OK y rebuild -r server OK (8.984 s). xmake build testbot OK y rebuild -r testbot OK (3.453 s). Sin errores pendientes.
- tools/correr_testbot.bat no existe en este clon. Equivalente aislado build/t95-testbot, puerto 34995, maxplayers=4, UPnP/persistencia apagados: all = 12 escenarios/121 chequeos/0 fallas TODO PASS; reinicio handover=0, d = 1 escenario/3 chequeos/0 fallas TODO PASS. Procesos propios cerrados; logs testbot-all.log y testbot-d.log en esa carpeta.
- CRLF validado en las seis fuentes; revisadas condiciones de participante/area/vida, alcance de interruptor, opcode/radios y rutas por rango. No se ejecutaron comandos git ni se instalo/abrio GTA. Documentados TESTEAR_HOY, ESTADO, SISTEMAS, tabla de mision e interruptor.
- SIN REVISAR por Claude / SIN PROBAR EN JUEGO. ENet no valida el exe: falta calibrar escenas/colocacion y peds adjuntos al tren. T40 distribuye una vez por etapa; sin sitio seguro no crea ni reintenta. Quien ya usa vehiculo lo conserva; Smoke/reagrupacion/audio tactico siguen del dueno. Tec9 usa slot nativo compartido de SMG como equipo de mision existente.
