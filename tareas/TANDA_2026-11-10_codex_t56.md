# T56 - Desafios de a dos
Objetivo: comparar actividades nativas independientes, sin compartir premios ni SCM.
Archivos: client/src/CActivityChallenge.{cpp,h}, Main.cpp, CConfigLoader.{cpp,h}, CMinigameVs.cpp, CMissionSyncRules.cpp, COpCodeSync.cpp, PacketHandlers/Scripts.cpp; server/src/PacketHandlers/Scripts.cpp; shared/network/packets/scripts.h, packet_types.h, config.h; ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md y tanda Karimov.
Restricciones: CRLF, protocolo 0.4.27-alpha, MIT citado, sin instalacion. Resumen <=10 lineas.
Critica previa: no asumir dos jugadores ni autoridad de historia. Invitacion dirigida, sesion unica, aceptacion validada de nuevo, resultados inmutables y latido para abandono. La referencia no arranca directamente todos los juegos: conserva sus entradas nativas, evitando ejecutar misiones con globales sin inicializar.
Casos borde:
- 3-4 jugadores: se desafia al cercano disponible; solo dos participantes, sin espectadores en la tabla.
- Individual/mision/otra area/lejos: no recibe ni acepta; revalidar al aceptar.
- Muerte, desconexion, cambio de modo o abandono: victoria del otro; no tocar recompensa.
- Opcion apagada: ninguna captura de tecla, lectura ni paquete nuevo.
- Reintentos: sesion distinta y no aceptar resultados de una sesion anterior.
Aceptacion: client/server/testbot compilados, CRLF y diff verificados; revision cruzada y juego pendientes.

## Implementacion - SIN REVISAR / SIN PROBAR / SIN INSTALAR
Ampliacion: CSideMissionRules.cpp, CMissionMirror.cpp (aislamiento/reset), tools/testbot/testbot.cpp. Reservas #18 y #19.
- activity-challenge=true; protocolo 0.4.27-alpha, ACTIVITY_CHALLENGE al final. U primeros 20 s, Y/N por 20 s, dos participantes; N durante desafio abandona.
- Basquet (global byte 7696), blackjack/ruleta/rueda (ganancia neta local), Kickstart (33048), Blood Ring/triathlon (tiempo nativo, menor gana). Carrera/8-track/Dirt Track conservan CVsRace; baile, lowrider y billar excluidos.
- Consentimiento dirigido al primer remoto elegible a <=12 m/misma area; no teletransporta. Basquet requiere tomar pelota; casino requiere ponerse en el puesto nativo: se pulsa su entrada automaticamente. Estadios/triathlon usan LOAD_AND_LAUNCH_MISSION_INTERNAL (128/129/130), variante del triatlon local. No salta IP ni copia premios.
- Estadios/triathlon quedan independientes con opcion activa (sin menu/espejo coop); apagada preserva comportamiento anterior. Sesiones, remitente sellado, resultado inmutable, latidos y N para abandono; limpieza al cargar mundo.
- Referencia MIT: sa-coop Karimov-Akbar features/story/contest/Contest.cpp. Licencia completa ya en NOTICE.md por T53. Semantica de condiciones: gta-reversed Scripts/Commands/PadCommands.cpp y CameraCommands.cpp.
- Limitacion de entrada nativa: aceptar cerca pero fuera del puesto de casino no mueve al jugador; debe acercarse. Basquet no crea una pelota en la mano: debe recoger la de su juego. Ventana de inicio maximo 3 min. Prueba de estas entradas pendiente.

## Verificacion final
- Client completo -r: OK; server completo -r: OK (7.187 s); testbot completo -r: OK (3.047 s). Ultimo build cliente en build/t56_client_verified.log.
- Runner aislado build/t56_run.bat: 7 escenarios, 55 chequeos, cero fallas, TODO PASS (build/t56_tests_final.log). Caso g: 17 chequeos con cuatro clientes, incluyendo confirmacion al invitado, variante, tiempo negativo y liberacion al terminar ambos.
- Fin de basquet detectado por BB_17 nativo (salir antes = abandono); triatlon admite RACES_8 y tune nativo. Confirmacion de servidor antes del lanzamiento; muerte/arresto, N, desconexion y recarga limpian la sesion.
- Atribucion en NOTICE.md (reserva #20). Semantica de UpdateCompareFlag y etiquetas tomada de gta-reversed RunningScript.cpp y constructor ya existente de POOL2, sin parche nuevo por direccion.
- C++ CRLF y git diff --check verificados. Sin SCM, instalacion, GTA, binarios en dist ni publicacion. Revision cruzada y entradas/resultados en juego pendientes.
- Sin commit: este entorno declara .git de solo lectura y no permite elevacion. Liberadas reservas al entregar para no bloquear al orquestador; debe revisar y commitear.
- Commit sugerido: feat: desafios de actividades de a dos (SIN REVISAR). Cuerpo: Adaptado de sa-coop (Karimov-Akbar, MIT): src/client/features/story/contest/Contest.cpp; semantica gta-reversed RunningScript.cpp, PadCommands.cpp y CameraCommands.cpp.
