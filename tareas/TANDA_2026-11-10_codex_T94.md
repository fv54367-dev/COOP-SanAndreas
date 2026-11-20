# T94 - Running Dog (SMOKE2, 28)

## Reserva
client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, client/src/CScenePartner.cpp, client/src/CCutscenePartners.cpp; esta tanda, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md, docs/MISIONES_MODO.md. Sin git, SCM ni protocolo; CRLF. Resumen <=10 lineas.

## Critica previa y recorrido
Referencia del fork (1771 lineas), no asumir offsets/locales del vanilla. No convertir la persecucion en espera de todos ni sustituir al conductor de Smoke por un invitado. T46 ya aplica bajas: no duplicar dano. La perdida por distancia debe considerar a cualquier participante, conservando el limite rectangular nativo de 300 m y su NOT.
- 1-179: preparacion y SMOKE2A: rutas por rango y mocap T58/T86; sin ruta/clip valido, ocultar. Escuchar/caminar segun pista existente; no poses inventadas.
- 180-273: streaming, posiciones, auto 466 con Smoke y secuencias de huida. T40 entrega un auto por invitado cercano a pie cuando el dueno conduce el 466 inicial; conservar los autos de quienes ya conducen.
- 274-425: entrada al auto del dueno, pickup de pistola opcional, dialogo y destino. IS_CHAR_IN_CAR sigue del dueno; pickup T64/espejo existente, blips/textos espejo vanilla. Destino espera invitados a 50 m para estacionar varios autos sin amontonarlos.
- 426-548: tutorial de sprint, dialogo al acercarse a Smoke y activacion de guardias. No ampliar proximidades tacticas ni exigir que todos se acerquen a Smoke.
- 549-737: carrera del fugitivo, subida a 405, drive-by y escape a los 180 s. NPC y ruta permanecen del dueno; cualquiera puede matarlo por T46. Cada jugador puede usar vehiculos de calle; no generar autos en callejones durante persecucion.
- 738-789: fallo por Smoke/auto inicial, distancia 300 m y escape final. Extender SOLO el LOCATE 00F2 sin esfera y radios 300/300 del jugador del dueno a participantes vivos en su misma area. Escape al destino sigue siendo fallo nativo aunque alguien este cerca.
- 790-899: creacion del 466 y SMOKE2B. Rutas por rango con mocap o fuera de plano; no sentar invitados en asientos de NPC.
- 900-1231: camaras fijas de persecucion (CJ/Smoke corren, Smoke se cansa). Invitados fuera de plano por T86 durante widescreen y restauracion al terminar; teletransporte de entrada existente.
- 1232-1458: escena alternativa de escape del fugitivo por azotea: fuera de plano durante widescreen; no mover al dueno para seguir invitados.
- 1459-1771: audio, victoria/fallo y limpieza: espejo y ciclo general existente. Sin dinero/estrellas compartidos ni nuevas recompensas.

## Casos borde y aceptacion
- 2-4: una copia T40 por invitado a pie, suelo/colision/visibilidad; sin lugar seguro no crear. No embarcar a >120 m ni en otra area.
- F1 individual, jugadores tardios y abandono: ShouldIncludeNetworkPlayer excluye; no cuentan para persecucion ni embarque.
- Muerte/arresto usa fallo general; desconexion elimina participante de espera/persecucion y cancela embarque.
- Lejos/otro interior no evita fallo de distancia; escena restaura poses por T58/T86.
- coop-running-dog=false conserva comportamiento previo; dependencias existentes de reglas, vehiculos, espejo y escenas.
- Compilar client/server/testbot; regresion aislada si falta correr_testbot.bat; sin instalar/abrir GTA. Revision cruzada y juego pendientes.

## Resultado
- Implementados autos iniciales por invitado a pie, alcance de llegada 50 m y extension del chequeo 00F2 de perdida a participantes vivos/misma area, antes del NOT nativo. No se cambian las proximidades tacticas ni la ruta/tareas de NPC.
- SMOKE2A/B: rutas por rango existentes con mocap CScenePartner/CSceneMocap; sin ruta/clip, ocultos. Camaras por script (carrera/Smoke cansado/escape por azotea): ocultar/restaurar con T86. Calibracion visual pendiente.
- Interruptor cliente coop-running-dog (default true), lectura y guardado; false mantiene comportamiento previo. Dependencias de sistemas existentes documentadas.
- xmake build client OK (recompilo y enlazo, 28.594 s); xmake build server OK y rebuild -r server OK (9.078 s); xmake build testbot OK. Sin errores.
- No existe tools/correr_testbot.bat en este clon. Equivalente aislado ENet build/t94-testbot, puerto 34994, UPnP/persistencia apagados: testbot all = 12 escenarios/121 chequeos TODO PASS; servidor reiniciado con handover=0, testbot d = 3 chequeos TODO PASS. Ambos procesos propios cerrados.
- CRLF verificado en las seis fuentes editadas. Sin git, shared/network, SCM, instalacion ni apertura de GTA. No se porto codigo externo nuevo (escenas MIT previamente atribuidas en NOTICE.md).
- Documentados sistema, interruptor, fila de mision, Pendiente y pruebas Running Dog. SIN REVISAR / SIN PROBAR EN JUEGO.
- Limites: T40 distribuye una vez por intento, sin reintento si no hay sitio seguro; invitados con vehiculo conservan el suyo. No generar autos durante persecucion a pie: usar vehiculos de calle como el original. Textos/tutorial reflejan al dueno; proximidad a Smoke y liberacion tactica de guardias siguen del dueno. La huida por tiempo/destino sigue siendo fallo aunque haya un invitado cerca.
