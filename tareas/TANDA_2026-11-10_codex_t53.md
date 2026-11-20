# T53 — César Vialpando coop

Objetivo: adaptar el concurso lowrider de sa-coop Karimov-Akbar (MIT) a participantes y autoridad locales; sin VS ni SCM.
Archivos reservados en tablero #12: client/src/CCoopLowrider.{cpp,h}, CConfigLoader.{cpp,h}, CMissionSyncRules.cpp, COpCodeSync.cpp, CCoopMission.cpp, CMissionMirror.cpp, CCameraMirror.cpp, Main.cpp, Hooks/DamageHooks.cpp (reserva #13), ambos PacketHandlers/scripts.cpp; shared/network/{packets/scripts.h,packet_types.h}, shared/{config.h,mission_ids.h}; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/{REQUISITOS,MISIONES_MODO}.md y tanda sacoop_karimov.
Restricciones: CRLF C++; no tocar scm/, GTA ni instalar; protocolo de un solo dueño, 0.4.24-alpha; no publicar binarios.
Aceptación: client/server/testbot compilan; interruptor coop-lowrider true, apagado conserva individual; revisión cruzada y juego pendientes. Respondé con resumen de ≤10 líneas.

## Crítica previa
- El plan original no identifica la sesión: agregar identidad de concurso para rechazar puntos atrasados y duplicados.
- La referencia supone host de conexión; acá debe usarse la autoridad de misión y filtrar F1/participantes también en server.
- No basta borrar la chica: restaurar frenos, cámara, HUD y terminar BDISPLY al salir, fallar o desconectar.
- Verificar semántica gta-reversed AEAudioHardware::GetBeatInfo / AEStreamThread::GetTrackPlayTime; el reloj remoto debe funcionar incluso con tiempo local negativo.

## Casos borde
- 3–4: puestos ordenados por id, autos y chicas distintos, sumar cada beat una vez por participante.
- Individual o interruptor apagado: sin sincronizar SWEET6 ni cambios del concurso local.
- Abandono/desconexión invitado: limpiar sus recursos y dejar de esperar sus puntos; dueño termina para todos.
- Muerte/arresto: conservar el fallo colectivo existente; no convertirlo en abandono voluntario.
- Interior/lejanía: no crear en otra área; buscar espacio libre antes de colocar el auto.
- Stream local trabado: sustituir reloj de GetBeatInfo por dueño, incluso cuando la llamada original devuelve negativo.
- Fin de canción: espera acotada para últimos puntos; no tocar dinero ajeno ni activar VS.

## Resultado
- Concurso por equipos adaptado de sa-coop (Karimov-Akbar, MIT), con licencia completa en NOTICE.md; sin VS ni cambios SCM.
- Protocolo 0.4.24-alpha, paquete COOP_LOWRIDER al final, autoridad y recepción de misión verificadas por el server; identidad de intento y deduplicación por participante/beat en el dueño.
- Autos con hidráulica y chicas por invitado, colocación por id con búsqueda libre, cámaras y BDISPLY propios; juicio local contra reloj del dueño a 10 Hz, incluso con tiempo local negativo.
- Limpieza en fin, abandono, desconexión, cambio de autoridad y carga de partida. Conserva estado de cierre del auto propio; borra prestado y chica; no cambia reglas de muerte ni activa VS.
- Client recompilado completo con -r OK (29.079 s); último ajuste de limpieza fuera de autenticación recompilado OK (2.297 s). Server OK (7.234 s), testbot OK (2.437 s).
- Testbot canónico ejecutado con copia del bat que restringe rutas/cierre de procesos a build/t53_testserver*: 5 escenarios, 22 chequeos, cero fallas, TODO PASS. No se instalaron binarios ni se tocó ningún GTA.
- CRLF y git diff --check OK. BDISPLY verificado en original: bytes a4030942444953504c5900 a 0x1CDEA.
- Pendiente revisión cruzada y prueba en juego de 2–4, hidráulica remota y dinero de cada jugador. El sync normal de vehículo transmite movimiento/rotación; no se afirma que la animación de suspensión se vea hasta probar.

## Commit bloqueado por el entorno
- `git add` falló: `Unable to create .git/index.lock: Permission denied`. El sandbox declara `.git` de solo lectura y no permite elevar permisos; no hay commit ni publicación.
- Mensaje y cuerpo con cita MIT preparados en `build/t53_commit.txt`; título: `feat: concurso de Cesar Vialpando en coop por equipos (SIN REVISAR)`.
- Al commitear, preservar el bloque de coordinación T53/T54–T61 ya agregado por el dueño en la tanda sacoop_karimov: esta tarea solo añadió su sección de resultado T53.
- Reservas #12/#13 liberadas al cerrar con el commit pendiente documentado, para no bloquear la revisión de Claude.
