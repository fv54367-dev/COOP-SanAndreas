# T85: cabeza de la copia en cinematica

Objetivo: una sola fuente de pose de cabeza mientras la escena posee el ped remoto.
Archivos: client/src/CScenePartner.cpp, client/src/CCutscenePartners.cpp, client/src/CCutscenePartners.h, client/src/CAimSync.cpp, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Restricciones: CRLF C++; sin protocolo, SCM, instalacion ni GTA. Red tablero no responde; seguir tandas en serie.
Aceptacion: client/server/testbot compilan, testbot TODO PASS; revision Claude y prueba visual pendientes. Resumen <=10 lineas.

Casos borde:
- 2-4 jugadores: propiedad por ped y handle; no bloquear mirada del local ni de espectadores.
- F1 individual, distinta escena o interruptor cutscene-partners apagado: no suprimir contexto remoto.
- Muerte, desconexion, timeout y cambio de elenco: restaurar solo peds con handle vigente.
- Interior/distancia: criterio de participacion existente; ninguna nueva autoridad.
- Salto/fin y escenas consecutivas: liberar bloqueo nativo y tareas de escena, sin recuperar objetivos/punteros viejos.
- No sistema nuevo: correccion bajo cutscene-partners existente, sin cambio de protocolo ni reglas de mision.

Evidencia: CScenePartner::Live combina clips/fidgets con LookAt forzado cada 200 ms (blend 500 ms). CAimSync aplica camara remota durante CPlayerPed::ProcessControl. gta-reversed PedIK.cpp RotateTorso modifica quaternions y bloquea IK; Ped.cpp SetLookFlag/ClearLookFlag y PlayerPed.cpp ProcessControl generan mirada desde camara FOLLOWPED. Ragdoll/IKChainManager.cpp LookAt permite bForceLooking saltar CanAcceptLookAt; por eso tambien se quita el LookAt propio de Live. TaskSimpleTriggerLookAt.h describe la tarea (su .cpp local no implementa ProcessPed); no atribuirle una implementacion ausente.

## Implementacion y verificacion
- Live deja la cabeza a la animacion, sin LookAt forzado; Hold cancela IK entrante tambien en poses WALK/CAR/WAY y durante carga. Facial y resto de coreografia conservados.
- Saved captura bDontAcceptIKLookAts antes de cambiarlo y Restore lo recupera solo con player/handle vigente. ControlsPed exige elegibilidad, escena actual y handle; CAimSync guarda camara local siempre para mantener simetria ApplyLocalContext, pero no aplica la remota a la copia poseida.
- Sin objetivos/punteros de mirada guardados: al salir los sistemas normales vuelven a producirlos con estado fresco. Sin sistema/interruptor nuevo.
- Client recompila afectados y enlaza OK (3.843 s); server -r OK (9.484 s), testbot -r OK (3.547 s). Sin instalar ni jugar. Revision cruzada pendiente; .git solo lectura, commit queda al orquestador.

Resultado: runner aislado build/t85_testbot.bat (solo sus servidores, sin tocar GTA) exit 0; build/t85_testbot.log: 11 escenarios/110 chequeos + handover apagado 1/3, total 12/113, cero fallas, TODO PASS. CRLF de los cuatro C++ y git diff --check OK. Compilado, sin probar visualmente.
