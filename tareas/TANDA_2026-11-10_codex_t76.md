# T76 - Trenes random duplicados
Objetivo: un solo juego de trenes random, generado por el host.
Archivos: client/src/CTrainSync.cpp, client/src/CPatch.cpp y esta tanda.
Documentacion prevista: TESTEAR_HOY.md y ESTADO.md, bloqueados por reserva #48 T78; no tocar hasta coordinar.
Restricciones: CRLF C++, sin SCM, protocolo ni instalacion. Resumen <=10 lineas.
Aceptacion: evidencia gta-reversed/binario; compilan client/server/testbot.
Casos borde:
- 3-4 jugadores y migracion: nuevo host genera, seguidores limpian randoms locales.
- F1 individual, interiores y distancia: no cambian la autoridad global del tren.
- Muerto/desconectado/reconexion: sin referencias a vehiculos borrados; bloqueo antes del rol.
- Ocupantes: bajar antes de borrar, incluidos pasajeros y jugadores remotos.
- No borrar trenes de mision ni copias identificadas, aunque un script cambie su flag.
- train-sync=false conserva el camino anterior, sin limpieza ni bloqueo nuevo.

## Evidencia y cambio
- gta-reversed Train.cpp: DisableRandomTrains (0x6F5DB0) solo escribe bDisableRandomTrains; RemoveOneMissionTrain (0x6F5DC0) recorre y destruye la cadena sin expulsar ocupantes ni limpiar apoyos. Por eso se conserva borrado explicito con limpieza previa.
- DoTrainGenerationAndRemoval (0x6F7900) es un stub en esta referencia: se verifico el exe 1.0 US local con pefile/capstone. 0x6F7AE8 lee 0xC38010 (Train.h bDisableRandomTrains); 0x6F7AEF salta a 0x6F8160 si esta activo. Bloquea generacion, no limpia trenes existentes.
- CPatch::RevertTemporaryPatches, llamado en handshake antes de PLAYER_ASSIGN_HOST, habilitaba randoms. ProcessGeneration solo bloqueaba seguidores ya autenticados. Se mantiene deshabilitado hasta el rol host y se bloquea tambien antes de autenticacion.
- OnBecameFollower limpiaba una sola vez y saltaba cadenas ocupadas (duplicado persistente). Ahora se limpia al recibir el rol y cada 200 ms; se baja a ocupantes por WARP_CHAR_FROM_CAR_TO_COORD antes de borrar, con RemoveReferencesToDeletedObject para apoyos/techo. Referencia CharacterCommands.cpp WarpCharFromCarToCoord y camino existente CNetworkVehicle::DestroyGameVehicle.
- Se preservan trenes de mision y copias por identidad, incluso si un script les quita bMissionTrain. Sin limite arbitrario de 16 cabezas/longitud de cadena para limpiar.
- Log train-local-random con cabeza/modelo/via/posicion: una vez por cadena eliminada en seguidor. Ni nuevo protocolo ni cambios de server; conserva autoridad m_bIsHost en ambos lados.
- train-sync=false no entra al bloqueo/limpieza nuevos; RevertTemporaryPatches habilita randoms como antes.
- Falta revision cruzada y prueba en juego. No instalado.

## Texto pendiente para documentos compartidos (reserva #48 T78)
TESTEAR_HOY.md, seccion trenes:
Con train-sync=true, esperar varios trenes en estacion: mismo tren completo en todos, sin otro local atravesandolo. Entrar tarde, cargar partida y reconectar; si habia un random local ocupado, bajar sin crash al limpiarlo. Revisar train-local-random en CoopAndreas_mirror.log del invitado (una linea por tren eliminado; puede no salir si no habia randoms). Con 3-4, desconectar host: el nuevo genera y los demas conservan solo sus copias. Probar tren de mision del dueno, F1 individual y tren lejano/interior; repetir train-sync=false. SIN REVISAR / SIN PROBAR / SIN INSTALAR.
ESTADO.md, Pendiente:
T76 trenes duplicados: bloqueo de generacion antes del rol y limpieza periodica de randoms locales, incluidos ocupados; conserva misiones/copias y agrega train-local-random. Evidencia gta-reversed Train.cpp y exe 0x6F7AE8/0x6F7AEF. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

## Validacion final
- xmake build -r client OK (37.125 s), server OK (15.188 s), testbot OK (3.281 s).
- git diff --check OK y todos los saltos C++ siguen en CRLF.
- No se ejecuto juego ni se instalo. Testbot compilado; no se ejecuto su suite (no hay cambios de servidor/protocolo).
- Documentos compartidos pendientes por choque de reserva #48, informado al usuario. Texto listo arriba.
- Cambios sin commit: el sandbox de esta sesion deja .git en solo lectura; reserva #49 mantenida hasta commit por orquestador.
