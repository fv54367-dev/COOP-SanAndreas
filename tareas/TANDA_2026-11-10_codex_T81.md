# T81 — intro local para todos
Objetivo: intro id 2 individual, sin lock, autoridad ni espejo; interruptor intro-independent=true.
Archivos: client/src/CMissionLock.cpp y .h, CCoopMission.cpp, CMissionSyncRules.cpp, CConfigLoader.cpp y .h, PacketHandlers/Scripts.cpp, COpCodeSync.cpp (todos bajo client/src/), ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md y esta tanda.
Restricciones: C++ CRLF; sin SCM, protocolo, instalación ni juego. Reserva #54; T80 ya committed 40b893a.
Aceptación: client/server/testbot compilados; TODO PASS; diff y CRLF verificados.
Crítica previa del plan: exceptuar solo el lock deja autoridad del host y paquetes entrantes activos.
Casos borde:
- 2–4 intros simultáneas: cada una local, sin cambiar F1 ni registrar un dueño.
- F1 individual/coop: intro siempre local; interruptor apagado conserva comportamiento anterior.
- Desconexión/muerte: no abortar la intro de otros ni limpiar su cámara o controles.
- Host o invitado en intro: bloquear replay entrante; al terminar recupera reglas normales.
- Big Smoke: marcador espera también al jugador con runningMission=2, aunque su copia está cerca/en otra área.
- CCutscenePartners: IsLocalIndividualMission impide preparar, confirmar o insertar compañeros durante INTRO.
Referencia: gta-reversed Scripts/RunningScript.cpp Process/ProcessOneCommand: lista activa y m_bIsMission delimitan vida del script. Referencia INTRO.txt (fork GPL) solo inspeccionada; sin copiar ni modificar SCM.
Respondé con resumen de ≤10 líneas.

## Resultado
Implementado: excepción local del id 2 en comprobación de marcador, inicio y resolución de lock; no emite solicitud de lock.
IsLocalIntroRunning valida lista activa (también reconoce nombre INTRO si no se vio 0417); autoridad false e individual true durante su vida, sin cambiar F1.
Replay de opcodes y handlers de mensajes/audio/cámara/teletransporte/skip quedan excluidos durante INTRO. No se modifica CMissionMirror: ya excluye ids 0–10 (CMirrorPolicy.h); el cambio de autoridad también bloquea snapshots y progreso saliente.
Big Smoke: AreMissionPlayersNear trata runningMission=2 como ausente aunque su ped esté cerca; no excluye al jugador del grupo requerido ni lo teletransporta para resolver la espera.
Escenas: CCutscenePartners::Eligible ya exige !IsLocalIndividualMission; no prepara ni confirma compañeros en INTRO. CCutsceneSkipVote::Process oculta todas las copias mientras corre una cinemática, y devuelve visibilidad al terminar. Sin cambios en esos módulos.
Al terminar el script la excepción desaparece; continúan los scripts originales y las reglas de progreso existentes, sin copiar guardados ni forzar progreso global.
Verificación: client 36,797 s y recompilación final explícita 2,422 s; server -r 11,703 s; testbot -r 3,922 s. Todos OK.
Regresión: copia existente build/t80_testbot.bat (servidores dentro del repo y kill limitado a rutas exactas); 11 escenarios, 101 chequeos, 0 fallas, TODO PASS.
CRLF en los 8 archivos C++ tocados y git diff --check OK. ESTADO, TESTEAR_HOY e INTERRUPTORES actualizados.
Sin instalación, juego, SCM ni protocolo. SIN REVISAR por Claude / SIN PROBAR en GTA; testbot no valida la intro del cliente.
Sin commit: .git de solo lectura en esta sesión; reserva #54 conservada para que el orquestador commitee y libere.
