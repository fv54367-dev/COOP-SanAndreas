# T83: una poblacion visible por interior

Objetivo: autoridad de interior elegida por servidor, dependientes locales fuera de la replica y animaciones tardias.
Archivos: client/src/CInteriorOwnership.cpp, CInteriorOwnership.h, CNetworkPed.cpp, CNetworkPed.h, CConfigLoader.cpp, CConfigLoader.h, CStreamingSync.cpp, CSpawnWatch.cpp, CNetworkPedManager.cpp, Hooks/WorldHooks.cpp, PacketHandlers/Peds.cpp; server/src/CNetworkPlayer.h, CNetworkPed.h, PacketHandlers/Peds.cpp, CHandover.cpp, CNetwork.cpp; shared/config.h, shared/network/packet_types.h, shared/network/packets/peds.h; tools/testbot/testbot.cpp; ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md y esta tanda.
Reservas #56-58 aceptadas como codex@local (continuidad en serie de T81/T82).
Restricciones: CRLF; sin SCM, instalacion ni GTA. Protocolo pasa de 0.4.39 a 0.4.40-alpha; unico escritor de protocolo en esta tanda.
Aceptacion: client/server/testbot compilados; TODO PASS; diff y CRLF; resumen <=10 lineas. SIN REVISAR hasta Claude.

Critica previa: el area no identifica una tienda (hay varios interiores con igual area); hacen falta tambien coordenadas. Elegir por ID en cada cliente no garantiza primer ingreso. Un MISSION_CHAR no identifica la procedencia: BARB/JFUD crean dependientes asi. No borrar sus handles: el SCM local los sigue usando. Se conservan actores de servicio locales ocultos, sin red; solo el dueno publica la presentacion. No modificar ni eliminar actores de misiones por su modelo.

Casos borde:
- 2-4 jugadores y entradas simultaneas: arbitraje unico en servidor por orden de registro; cada respuesta identifica area y posicion.
- F1 individual: conserva scripts y servicios propios; actores de mision no se clasifican por modelo.
- Salida, muerte o desconexion: membresia vence y propietario restante elegido; no conservar punteros de jugadores borrados.
- Tiendas distintas con igual area: distancia tambien delimita membresia y supresion.
- Dependientes de BARB/JFUD/CLOTH/AMUNAT/TATTO/PSHOP/BAR: conservar handles/tareas locales para no romper el servicio.
- Interruptor nuevo interior-population-sync=true; false conserva elecciones y filtros previos.

Evidencia inicial: WorldHooks publica Char.Create via CWorld::Add; cada SCM local crea dependientes (BARB:242, JFUD:192, CLOTH:299). IsMissionPed exceptua cualquier MISSION_CHAR durante una mision. InteriorGroup_c SetupShopPeds 0x594C10 y SetupPeds 0x596890 son wrappers NO reimplementados en gta-reversed; TaskInteriorShopKeeper::ControlSubTask 0x675740 usa SHOP_CASHIER. Animaciones existentes: PED_TASK_STATE con bloque/hash y PED_ANIM_SET; los selectores named/relevant descartan poses detenidas y priorizan el grupo antes del peso.

## Implementacion
- INTERIOR_POPULATION al final del enum; PED_SPAWN lleva clasificacion de servicio y area, conservadas en reenvio/late join/traspaso. No se tocan scripts ni offsets. Elegido: primer registro observado por servidor (el orden fisico con latencia no puede conocerse).
- Los servicios BARB/JFUD/CLOTH/AMUNAT/TATTO/PSHOP/BAR conservan actores nativos ocultos (necesarios para handles SCM), no sus publicaciones. No se eliminan personajes por modelo; los actores de scripts de mision conservan su categoria incluso en CWorld::Add antes de asignar MISSION_CHAR. Proxies de tiendas no sincronizan dinero/progreso.
- Cliente espera decision antes de publicar, reaplica ocultamiento por frame, preserva takeover recibido antes del cambio de rol y adopta actor local compatible al traspasar para conservar sus tareas. Si no hay actor compatible, conserva pose/AI neutral del espejo; no inventa tareas de tienda.
- Poses named detenidas aceptadas y prioridad por peso en interior; resend de poblacion cada 500 ms, bloque/hash por la infraestructura existente (carga y reintento existentes). Named conserva bit PLAYING (gta-reversed Animation/AnimBlendAssociation.h:21); las poses agrupadas no se sustituyen por TASK_STAND_STILL al traspasar. El caso de herramientas/coreografia del servicio sobre actores auxiliares necesita prueba real.
- El area viaja con el spawn y al aprender area por cercania tambien se corrige el ped real. dup-ped agrega visibilidad/area: un proxy invisible y su copia no equivalen a dos personajes visibles.
- Logs cliente de eleccion/publicacion/ocultamiento/borrado/adopcion/animacion y servidor de rechazo/traspaso. Mismo interruptor en todos; false conserva el camino previo. Los interruptores de animacion/traspaso existentes siguen siendo dependencias documentadas.

## Verificacion final
- Cliente reconstruido completo con xmake build -r client: OK (51.563 s); server -r: OK (11.297 s); testbot recompilado: OK (3.875 s). Sin errores ni warnings C en esos logs.
- build/t83_testbot_final.log: 11 escenarios/110 chequeos mas handover apagado (1/3), total 12 escenarios, 113 chequeos, cero fallas, TODO PASS. Escenario l agrega 12 verificaciones: invitado primero, host primero, 4 jugadores, misma area/diferente ubicacion, publicacion no autorizada, mision preservada, identidad en takeover, salida, desconexion, reentrada, simultaneidad y lease vencido. No prueba render/animaciones/handles del exe.
- CRLF OK en 20 C++ modificados; enum y nombres de 142 paquetes coinciden en orden; git diff --check OK. No SCM ni instalacion ni GTA. La reconciliacion de sombras se hace por actor despues de 3 s: takeover parcial no oculta dependientes faltantes y la poblacion que quedo al salir todos no se duplica al reentrar.
- SIN REVISAR por Claude / SIN PROBAR EN GTA / SIN INSTALAR. Sin commit: .git es solo lectura en esta sesion; reservas #56-59 quedan para commit/liberacion del orquestador. Archivos T88/T89/T90 aparecidos durante la sesion no se tocaron.
