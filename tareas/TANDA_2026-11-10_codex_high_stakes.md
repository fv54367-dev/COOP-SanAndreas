# Tanda Codex: High Stakes Low-Rider coop (2026-11-10)

Pedido: otro PR, separado del trabajo de Claude. Base main 6a2a978; rama codex/high-stakes-coop. Inicio solo cuando todos los jugadores coop conducen lowriders cerca del marcador; transicion CESAR1 -> CPRACE sin menu de secundarias/VS; conservar NPC; checkpoint/progreso por participante; esperar meta o cercania de todos antes del resultado original.

Archivos reservados por esta tanda (red tablero falla en esta PC, se usa la convención): client/src/CHighStakesRace.{h,cpp} nuevos; client/src/CMissionSyncRules.cpp, CSideMissionRules.cpp, CMissionMirror.cpp, CMirrorRaceCheckpoints.cpp, CMirrorBlips.cpp, CConfigLoader.{h,cpp}, Main.cpp, PacketHandlers/scripts.cpp; shared/config.h, shared/network/packet_types.h, shared/network/packets/scripts.h; server/src/PacketHandlers/scripts.cpp; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md y esta tanda. Cambios acotados, CRLF; sin SCM, sin instalacion mientras juegan, sin ramas de Claude. Protocolo 0.4.29-alpha, dueño de version/paquete de esta tanda Codex; rebasear si main avanza.

Critica previa de Codex (solo lectura):
- CESAR inicia dos misiones, 36 y despues 35; no basta quitar el menu a toda CPRACE. Detectar la transicion y globals originales verificadas (cs1_race_is_go=9344, race_selection=1408, show_race_selection=484) y no incluir otras carreras.
- No ejecutar una carrera entera por invitado: duplicaria NPC. Una autoridad lee la ruta original CPRACE y calcula checkpoints propios por la posicion remota; los invitados solo dibujan su checkpoint.
- El host puede llegar antes: retener el resultado antes de la fase 13, no ejecutar el resultado ni despawn mientras faltan jugadores. Evitar que el checkpoint/espejo de la camara del host tape al propio.
- Participantes fijados al inicio, sin invitados tardios; no basta chequear solo el ultimo checkpoint ni un paquete cliente que diga que gano. El dueño comprueba la posicion en orden; se permite cercania a meta despues de terminar el dueño (pedido expreso).

Casos borde:
- 3-4 jugadores: cada uno conductor de su lowrider, casilla de salida propia y recorrido propio; NPC comunes.
- F1 individual no cuenta ni recibe; solo conserva el flujo original sin oferta extra de carrera de historia.
- Desconexion/abandono: deja de contar; muerte/arresto conserva fallo comun; sin retencion permanente por ped ausente.
- Otra area/lejos: no inicia ni pasa checkpoints; la meta exige vehiculo y misma area.
- Reintento/carga: sesion nueva, borrar marcadores y restaurar control/coche, no reutilizar punteros de script.
- Interruptor high-stakes-coop=false conserva exactamente el camino previo; no tocar carreras libres ni el concurso lowrider.

Referencia: Karimov-Akbar/sa-coop (MIT), commit 4135fc5, features/story/race/Race.cpp (ruta y ranking de CPRACE); gta-reversed TheScripts.cpp / RunningScript.cpp / ScriptCommands / VehicleCommands. Verificar firmas originales y comandos antes de usarlos.
Aceptacion: client/server/testbot compilan, testbot PASS, CRLF y diff --check; pruebas en juego del dueño con 2-4. Responder resumen <=10 lineas. SIN REVISAR hasta Claude revise el diff.

Integracion adicional reservada: client/src/COpCodeSync.cpp y client/src/CCameraMirror.cpp para no imponer control/camara del dueño al corredor invitado.

Archivo adicional reservado: client/src/CVsRace.cpp. Hallazgo: detecta dos missionId=35 y borra NPC automaticamente; excluir solo High Stakes activa. Retencion final ajustada: continuar fase 12 para que NPCs sigan corriendo, retrasar solo fase 13, sin dormir el script.

Prueba adicional reservada: tools/testbot/testbot.cpp, escenario i de STORY_RACE (autoridad, destinatario, F1, datos no finitos, fin).

## Implementacion
- CHighStakesRace, interruptor high-stakes-coop. Participantes capturados en CESAR, lowrider conductor y 12 m; segunda comprobacion en el cilindro de grilla CESAR1.
- Transicion original: cs1_race_is_go=9344, race_selection=1408, show_race_selection=484; firma 03A4 CPRACE verificada en original main.scm. No SCM ni offsets nuevos. CPRACE compartido se salta oferta secundaria; VS automatico excluido solo durante esta carrera.
- Datos CPRACE de referencia MIT Race.cpp: stage49, vueltas224, count250, racers256, player442, radios322-324, ruta454/536/618 (82). Autoridad comprueba secuencia; invitado dibuja su checkpoint. NPC comunes. Unica colocacion por sesion en grilla separada de NPC; countdown congela y GO libera coche/control.
- Meta: retrasar resultado/fase13, mantener fase12 procesando NPC; esperar recorrido o 12 m en lowrider. Resultado original del dueño conservado. Desconexion/F1 salen del grupo; muerte pide fallo comun. Carga descarta handles de pools viejos sin tocarlos.
- 096E verificado en exe: caso 0x4784E6 lee flag LOW_RIDER bit25 (0x478516..0x47851F). Se consulta handling directamente para no pisar ScriptParams ni reentrar hooks de opcode dentro de UpdateCompareFlag.
- STORY_RACE al final del protocolo, 0.4.29-alpha; remitente sellado por servidor, destino unico, F1/late/scripts filtrados, radios/coordenadas/heading finitos y acotados. El cliente valida sesion/secuencia, no acepta END antiguo y limpia con timeout.
- Escenario i en testbot: destino, spoof, individual, fuera de mision, radio invalido, NaN, llegada y fin. No valida conduccion/camaras/animaciones: esas pruebas son del dueño.

Estado: compilacion y pruebas en curso. SIN REVISAR / SIN PROBAR / SIN INSTALAR. Resumen final <=10 lineas.

Rebase sobre main 3708865 (T56/T58): se conservan ActivityChallenge y CutscenePartners; STORY_RACE se agrega al final, protocolo propio 0.4.29-alpha y testbot i (g/h ya ocupados por main).

Revision propia final: CPRACE 35 tambien era excluida por IsLocalIndividualSideMission de la capa espejo: excepcion solo para High Stakes activa. La firma original de impresion temprana de derrota (229@ > 0) esta en main.scm 0x10261C; se aplaza esa comparacion concreta mientras se espera el resultado. Locales 454/536/618 originales verificados con asignaciones en 0x10394C/0x103956/0x103960 (valores de ruta reales leidos del script, sin hardcodearlos).

## Validacion final (base main 3708865)
- Client -r OK (93.641 s); server OK (11.0 s); testbot -r OK (3.735 s), version 0.4.29-alpha.
- Testbot all a/b/c/e/f/g/h/i: 8 escenarios, 76 chequeos, 0 fallas TODO PASS. Escenario d con handover=0: 3 chequeos, 0 fallas TODO PASS. Total 79 chequeos; i aporta 10 de red para High Stakes.
- Se verifico que volver de F1 en medio de una mision no permite reingreso tardio (politica existente): la prueba i inicia otra mision antes de validar la siguiente llegada, no modifica esa regla.
- CRLF conservado en todos los .cpp/.h tocados; BOM de Main.cpp conservado al principio; git diff --check OK. Ningun SCM modificado. BUILD sin tocar para que lo estampe CI.
- SIN REVISAR por Claude / SIN PROBAR en juego / SIN INSTALAR. Las pruebas automaticas no ejecutan conduccion, grilla, camaras, NPCs ni meta. Pruebas manuales en TESTEAR_HOY.md.

Revision de la carga: PREPARE se envia en SCRIPT_NAME CPRACE y se renueva cada 500 ms antes de que exista ruta/grilla valida. Asi el cliente excluye el auto-VS antes de recibir NPCs y no expira su sesion mientras se cargan modelos. El servidor admite PREPARE temprano (sin teletransporte ni control); RACING sigue exigiendo m_bOnMission. Escenario i agrega esta comprobacion.

Validacion de la proteccion de carga: cliente -r 61.453 s, server 1.532 s, testbot -r 3.719 s OK. all a/b/c/e/f/g/h/i: 77 chequeos TODO PASS (i: 11); d: 3 PASS. Total final 80. El CI del primer commit compilo/publico en 4m41s; su job Todo junto fue cancelado por concurrencia de GitHub, no por compilacion. Se repite CI para este ajuste.
