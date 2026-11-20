# T82 - copias de NPC de mision
Objetivo: diagnosticar fallos y resolver especiales por nombre local sin destruir peds del invitado.
Archivos: client/src/CNetworkPed.cpp, CNetworkPed.h, CNetworkPedManager.cpp, PacketHandlers/peds.cpp; ESTADO.md, TESTEAR_HOY.md y esta tanda. Reserva #55; documentacion de T81 continuada en serie, sin tocar su codigo.
Restricciones: CRLF C++; sin protocolo/SCM, instalacion ni juego. Correccion de sistema existente, sin interruptor nuevo.
Aceptacion: client/server/testbot compilados, TODO PASS, diff/CRLF; resumen de hasta 10 lineas.
Critica previa: no basta elegir slot: un fallo inicial elimina el registro; un slot sin refs puede estar reservado por el SCM; conservar identidad host y separar modelo local.
Casos borde:
- 2-4 jugadores: identidad por pedid; nombre compartido reutiliza modelo local, independiente del slot remoto.
- F1 individual o SCM local: nunca reasignar slots con refs, flags de reserva o carga pendiente.
- Diez slots ocupados: conservar registro y reintentar, log unico por motivo.
- Cinematica/interiores: registrar supresion, distancia, destruccion externa, area y visibilidad sin cambiar reglas de escena.
- Muerte/desconexion: limpieza existente; reemplazo del mismo pedid borra copia vieja antes de crear nueva.
- Reasignacion local del modelo: validar nombre y recrear copia; slot remoto no determina apariencia de copias antiguas.

## Evidencia y resultado
- Hipotesis confirmada en codigo: CreateGamePed rechazaba nombre distinto con refs; PED_SPAWN borraba el registro al fallar, sin reintento. No se afirma que explique todos los casos observados sin log en vivo.
- gta-reversed source/game_sa/Streaming.cpp:1783 RequestSpecialModel (0x409D10) elimina peds/objetos al reasignar; misma clave y entrada de disco reutiliza el modelo. RequestSpecialChar (0x40B450, :1774) suma base SPECIAL_MODELS_RESOURCE_ID. SDK enums/eScriptCommands.h:582 identifica LOAD_SPECIAL_CHARACTER=023C. Su cuerpo SCM no esta reimplementado en este checkout de gta-reversed; no se inventa una semantica del opcode. CutsceneMgr.cpp:833 reserva especiales con MISSION_REQUIRED, KEEP_IN_MEMORY y PRIORITY_REQUEST.
- Resolucion por clave/nombre: primero mismo nombre con datos de disco, luego slot sin refs, flags de reserva ni carga pendiente. No depende del numero remoto. Sin slot seguro, registro conservado y reintento limitado a 1 s.
- Reemplazo de spawn borra copia vieja antes de cargar; valida nombre local por frame y detecta puntero anulado por RegisterReference para recrear. Cache de nombres local y registro hosted conservan el nombre correcto para traspasos.
- ped-copy-fail reason/model/special/slot/refs: unico por registro y motivo; incluye pedid, area real/local, visibilidad y posicion. Registra fallos de validacion/carga/pool, supresion inicial o posterior, distancia, reemplazo, retirada del servidor, borrado externo y reasignacion. ped-copy-created permite reconocer recuperacion y slot elegido.
- Candidatas revisadas: CStreamingSync no distancia-corta MISSION_CHAR; CInteriorOwnership exceptua peds de mision compartida/autoridad. No se cambiaron sus filtros. CCutsceneSkipVote oculta jugadores remotos, no CNetworkPed. CutsceneMgr carga actores de escena aparte: Sweet en cinematica sigue necesitando corrida para determinar si es copia o actor de escena; diagnosticos de area/hidden ayudan cuando es copia.
- Build client completo 34.563 s y recompilacion final explicita de CNetworkPed.cpp 1.781 s; server -r 10.688 s, testbot -r 3.891 s: OK. Sin warnings C reportados.
- CRLF en los cuatro C++ y git diff --check OK. Pruebas manuales Ryder/Sweet y casos borde documentadas en TESTEAR_HOY; pendiente en ESTADO.
- SIN REVISAR por Claude / SIN PROBAR en GTA / SIN INSTALAR. Sin commit: .git de solo lectura en esta sesion; reserva #55 queda para commit/liberacion del orquestador.
- Regresion ENet real: build/t80_testbot.bat reutilizado (servidores dentro del repo, procesos limitados por ruta); 11 escenarios, 101 chequeos, 0 fallas, TODO PASS. No verifica render ni especiales del cliente.
