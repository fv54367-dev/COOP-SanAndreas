# T93 - Los Sepulcros / SWEET7 (20)

## Alcance y reserva
client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, client/src/CMissionSyncRules.cpp, client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, client/src/CScenePartner.cpp, client/src/CCutscenePartners.cpp; esta tanda, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md, docs/MISIONES_MODO.md. CRLF C++; sin git, SCM, protocolo, instalacion ni GTA. Resumen <=10 lineas.

## Critica previa
La referencia es el script del fork, no una huella del vanilla: no asumir locales/IP. El grupo nativo (Sweet y dos homies) llena el Greenwood; los jugadores no deben reemplazar aliados ni satisfacer IS_CHAR_IN_CAR del conductor. La persecucion de Kane no debe convertirse en espera de todos: solo los marcadores de salida/llegada esperan al grupo. Las consultas de dano ya pasan por T63, las bajas por T46: no sumar bajas ni dano dos veces. Las estrellas permanecen individuales. No extender LOCATE sobre Sweet a cualquier jugador: iniciaria escenas dejando al dueno afuera.

## Recorrido completo y escenas
- 1-400: SWEET7A, streaming, preparacion de cementerio/aliados/autos. Escena inicial: rutas por rank SWEET7A/#1/#2 y mocap T58/T86; sin ruta/clip coherente, ocultar. No crear aliados extra.
- 401-958: dialogos, ayuda de reclutamiento y agresion a Sweet/Ballas. Textos/HUD/blips por espejo existente; dano por invitado por T63 (051A), vida por T46. Los homies siguen siendo NPC del dueno: no falsificar reclutamiento con jugadores.
- 959-1749: tutorial, reclutamiento y trayecto en 492. Al conducir el dueno el auto inicial, un auto propio por invitado cercano a pie por T40; conservar vehiculos de invitados. Marcadores del convoy esperan con radio minimo de 50 m.
- 1750-2040: llegada/descenso con camara fija en cementerio. Invitados fuera de plano por regla widescreen T86; restaurar al terminar.
- 2041-2606: encuentro con Sweet, ataque al funeral, guardias y Kane. Conservar LOCATE del dueno para secuencias nativas; dano de invitados cuenta con T63/T46, sin duplicar tareas.
- 2607-2910: Kane muerto, huida en 445 y llegada a Grove. Crear copias para invitados a pie al conducir el dueno el 445 del cementerio; quien ya tiene auto lo conserva. Llegada espera al grupo a 50 m. Escena final fija: fuera de plano/restauracion T86.
- 2911-3185: escena alternativa de explicacion y presentacion de Kane, con traslados/camaras. Fuera de plano por widescreen; sin teletransportar al dueno hacia invitados ni copiar sus tareas.
- 3186-3980: combate y rutas de huida a pie; dano/vida por sistemas existentes. Proximidad tactica a Kane sigue del dueno (limitacion: no retargetear su ruta solo por cercania de invitado).
- 3981-4708: refuerzos, coche de Kane y persecucion alternativa. Bajas por T46 y agresion por T63; conservar autoridad NPC y coche/condiciones nativas, sin compartir estrellas.
- 4709-5238: subrutinas de combate, victoria/fallo, limpieza. Reutilizar ciclo de copias T40 y fallo/abandono general; no nuevos paquetes ni offsets.

## Casos borde
- 2-4: una copia por invitado a pie, colocacion T40 con suelo/colision/visibilidad; si no hay sitio seguro no crear.
- F1 individual, abandono y jugadores tardios: ShouldIncludeNetworkPlayer filtra; embarques se cancelan.
- Muerte/arresto: fallo comun existente; desconexion deja de contar en espera y cancela embarque.
- Otra area o >120 m: no embarcar; el marcador sigue esperando participantes validos.
- coop-los-sepulcros apagado: comportamiento previo; requiere toggles existentes de reglas, equipo/dano y escenas segun sistema.

## Resultado
- Implementado con sistemas T40/T58/T63/T46/T86 existentes; no se porto codigo externo nuevo (port MIT de escenas ya atribuido en NOTICE.md).
- `xmake build client`, `xmake build server`, `xmake build testbot`: OK; cliente recompilo sus fuentes y enlazo en 28,547 s. Sin errores de compilacion.
- No existe `tools/correr_testbot.bat` en este clon: equivalente aislado en `build/t93-testbot`, puerto 34993, UPnP/persistencia apagados. `testbot all`: 12 escenarios/121 chequeos TODO PASS; servidor reiniciado con handover=0 y `testbot d`: 3 chequeos TODO PASS. Ambos servidores propios cerrados al terminar.
- CRLF verificado en los seis archivos C++ tocados. Sin git, SCM, protocolo, instalacion ni apertura del GTA.
- Referencias gta-reversed: World.cpp::FindGroundZFor3DCoord (0x5696C0), TestSphereAgainstWorld (0x569E20), GetIsLineOfSightClear (0x56A490) para colocacion T40; CharacterCommands.cpp::HasCharBeenDamagedByChar (051A, ultimo danador o su vehiculo) e IsCharDead para T63/T46; RunningScript::UpdateCompareFlag para condiciones nativas. No se agregaron hooks, direcciones ni opcodes.
- Documentados interruptor, sistema, fila de mision, pendiente y prueba con bordes. Sin revisar / sin probar en juego; falta revision cruzada y calibracion visual de SWEET7A/camaras fijas y convoy con 2-4 por el usuario.
- Limites: invitados a >120 m/otra area no se embarcan; si T40 no encuentra lugar seguro no crea copia ni reintenta automaticamente ese trayecto. No da armas (el original no equipa a CJ en esta mision); usar equipo propio. Reclutamiento y proximidad tactica de Kane conservan al dueno; tiros/bajas de invitados cuentan por T63/T46, no por un contador nuevo.
