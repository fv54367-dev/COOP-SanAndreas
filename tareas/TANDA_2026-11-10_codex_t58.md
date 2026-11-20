# T58 — compañeros en cinemáticas
Objetivo: portar ScenePartner/SceneMocap de Karimov-Akbar (MIT), con autoridad de misión.
Archivos: client/src/CScene{Partner,Mocap,Game,Script}.cpp, client/src/SceneCompat/*, client/src/CCutscenePartners.{cpp,h}, Main.cpp, CConfigLoader.{cpp,h}, client/version.rc, PacketHandlers/Scripts.cpp (cliente/servidor), shared/network/packets/scripts.h, packet_types.h, shared/config.h; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md y tanda Karimov.
Restricciones: CRLF, sin SCM, sin instalar ni jugar. Resumen <=10 líneas. SIN REVISAR hasta revisión cruzada.
Crítica previa: la referencia supone host fijo; validar dueño actual, escena y orden de toma. Los avatares no deben competir con la interpolación de red. No insertar COOPCS encima del bloque temporal de una cinemática (DeleteCutsceneData elimina el último bloque).
Casos borde:
- Orden estable por id para 3–4 jugadores; individual, ausentes y no participantes excluidos.
- Desconexión, muerte, abandono, salto y cambio de escena liberan poses y props.
- No CJ/hidden: ningún extra visible. No modificar la ropa del CJ de T48.
- cuts.img ilegible: pose base. Interruptor apagado: ningún paquete, pose ni extracción.
Aceptación: client/server/testbot compilan; recursos embebidos y licencia MIT; revisión y juego pendientes.

Ampliación: CNetwork.cpp, CMissionMirror.cpp (limpieza antes de borrar peds/recargar), tools/testbot/testbot.cpp (escenario h de red). Reservas #21 y #22.

## Implementación — SIN REVISAR / SIN PROBAR / SIN INSTALAR
- Poses STAND, LEAN, SIT, STEP, HIDDEN y WALK; WAY y CAR también portados, con recursos RCDATA de lugares (663 bytes) y caminos (1.003.352 bytes), verificados en la DLL con pefile.
- Autoridad de misión y confirmación READY de la misma escena; proximidad inicial <120 m/misma área, caducidad 1,5 s, sesión/secuencia/toma y máximo tres compañeros ordenados por id. Servidor sella remitente, filtra participantes/individual y rechaza coordenadas/relojes no finitos.
- T48 conserva CJ propio: en el puesto del invitado, ese invitado ve al dueño con su propia ropa; no se duplica el CJ local. Los puestos geométricos son comunes, la identidad protagonista sigue siendo por espectador como en T48. Usa peds remotos vestidos, sin reconstruir TXD.
- COOPCS se genera en memoria leyendo únicamente cuts.img local; nada de animaciones distribuido. Extracción asíncrona, validación de chunks/claves, fallback STAND para WALK/WAY sin clips. Primera escena puede usar poses base si todavía no terminó la extracción.
- ReleaseAll al terminar/saltar, al cambiar escena/autoridad, perder participantes/latidos, desconectar, reiniciar y cerrar el mundo; restaura posición/sectores/área/visibilidad. No toca dinero, progreso ni SCM.
- Referencia MIT: Karimov-Akbar/sa-coop `cutscene-partner/ScenePartner.*`, `cutscene-clips/SceneMocap.*`, `shared/game/{Game,Types,Anim,Script}.*`; licencia completa y atribución en NOTICE.md.
- Semántica contrastada con gta-reversed `CutsceneMgr.cpp` (DeleteCutsceneData/RemoveLastAnimFile), `Animation/AnimManager.cpp` (GetAnimationBlockIndex y LoadAnimFile), `app/app_game.cpp`/Renderer.cpp. Desensamblado del exe local verificó 0x53E9F9: E8 E2 6C 01 00, llamada a ConstructRenderList 0x5556E0. Hook de sitio de llamada con guard de bytes y llamada vanilla conservada.

## Verificación
- Cliente completo `-r`: OK (30,328 s, build/t58_client_verified_final.log); recompilaciones posteriores de los archivos ajustados: OK (última build/t58_client_complete.log). Servidor: OK (6,703 s, build/t58_server.log). Testbot: OK (build/t58_testbot_final.log).
- Runner aislado dentro de build/: primera pasada completa, 8 escenarios / 66 chequeos / cero fallas (build/t58_tests.log). Caso h ampliado después: 14 chequeos / cero fallas (build/t58_h_verified.log), incluyendo dueño no host y su desconexión. Total cobertura final: 69 chequeos (55 anteriores + 14 de T58).
- Durante la ampliación se corrigió el harness: no bombear un Bot ya desconectado y contestar con rechazo desde el cuarto jugador individual para cerrar la oferta nativa. No se cambió el servidor de misiones por esos errores de prueba.
- CRLF, recursos y git diff --check verificados. Sin instalación, juego, publicación ni binarios en dist. Falta revisión cruzada y juego (TESTEAR_HOY.md).
- Sin commit: este entorno permite leer .git pero no escribirlo y prohíbe elevación. Se entregan cambios para revisar/commitear; reservas liberadas para no bloquear al orquestador.
- Commit sugerido: `feat: compañeros en las cinemáticas cooperativas`. Cuerpo: `Portado de sa-coop (Karimov-Akbar, MIT): ScenePartner, SceneMocap y vistas shared/game. Semántica verificada contra gta-reversed CutsceneMgr, AnimManager, app_game y Renderer.`
