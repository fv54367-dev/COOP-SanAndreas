# T66 — acciones con botón del invitado
Objetivo: HEIST2, WUZI2 y Catalyst en C++, originales intactos; no instalar ni jugar.
Archivos: CMissionGuestActions.{cpp,h}, handlers cliente/server MissionGuestActions.cpp, shared/network/packets/mission_guest_actions.h, packet_types.h, config.h, CConfigLoader.{cpp,h}, COpCodeSync.{cpp,h}, CAimSync.cpp, CMissionSyncRules.cpp, Commands/CommandHooks.cpp; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/{MISIONES_MODO,SISTEMAS}.md, tools/{inspect_t66,test_t66_actions}.py y esta tanda.
Restricciones: CRLF, sin SCM, sin instalar. Aceptación: client/server/testbot compilan, pruebas por misión, cita MIT. Resumen <=10 líneas.
## Crítica previa
No intercambiar $PLAYER_ACTOR global durante esperas: afecta otros scripts. La secuencia genérica identifica todos los player peds como dueño: no sirve para animar al invitado. Ejecutar secuencia nativa explícitamente en el actor seleccionado y reservar una interacción en el dueño. WUZI2 no coloca objeto ni anima en el original: cambia 48@, blip y precisión de guardias; no inventar objeto de misión. Catalyst pendiente en T63.
## Casos borde
- 3–4: una acción de colocación activa por misión; reserva de carga antes de animar, contador solo al completar.
- F1/abandono/desconexión: cancelar y liberar reserva; no escribir locales tras fin/reintento/carga.
- Muerte/arresto: fallo colectivo existente; limpiar solo animación propia de esta acción.
- Misma área y posición nativa obligatorias; lejanos no cuentan.
- Fin/reintento: epoch nuevo, secuencias deduplicadas, huella del bloque original.
- mission-guest-actions=false conserva anterior; no toca controles del dueño.
## Estado
Implementación terminada. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

## Archivos adicionales coordinados
Reservas #35–37: también COpCodeSync.{cpp,h}. ProbeCondition admite 00FF y aislamiento anidado de captura para llamadas nativas. No se toca CTaskSequenceSync: identifica a todo player ped como dueño y animaría al actor equivocado.
## Tramos portados y verificación del original
| Misión | Bloque original / huella FNV | Opcode/lugar y tramo |
|---|---|---|
| HEIST2 | 23822E, 19367 bytes, A56B6122 | 00FF en 23B69E, 00E1 en 23B6FF; tramo hasta 23BA37. Secuencia 23B751–23B8C0; creación/offset/contador se reproduce en dueño tras reserva, sin ejecutar controles 23B74A/23BA25 para el dueño. |
| WUZI2 | 16CF33, 27266 bytes, 4E273A6C | 00FF en 16ED89, 00E1 en 16EDDB, continuación 16EDE8–16EE21 (GOSUB 171652 cambia precisión). El original no anima ni crea micrófono: secuencia visual explícita del invitado, continuación del mundo original intacta. |
| RYDER3 / Catalyst | 0C0A67, 20209 bytes, 00AD32D8 | CREATE_OBJECT_NO_OFFSET 0C3680, física hasta SET_OBJECT_VELOCITY 0C3730 e índice 0C375B; HostThrow escribe slots originales, no contador de capturas. |
`tools/inspect_t66.py` verifica las tres huellas y desensambla solo los rangos pertinentes, sin escribir SCM. `build/t66_original.log`. La referencia llama 0C3680 a la creación, NO al inicio de RYDER3: corregido con tabla original.
## Implementado
Interruptor independiente mission-guest-actions=true; protocolo 0.4.32-alpha, MISSION_GUEST_ACTIONS al final, EVENT fiable. Filtrado local/red, epoch/secuencias, reserva única, cancelación, secuencias nativas por actor, objetos en dueño con identidad de CObjectSync y cleanup nativo. No mueve ni deshabilita controles del dueño. Apagado libera pendientes y conserva anterior. No SCM, instalación, juego ni publicación.
Catalyst requiere subirse físicamente sobre el caboose; no agrega conducción ni embarque automático (T62). Potencia por mantener/soltar disparo propio, con ayuda local. Se conservan los chequeos de captura originales de Ryder.
Referencias gta-reversed: RunningScript.cpp ProcessOneCommand/Process/UpdateCompareFlag; CharacterCommands.cpp DoLocateCharChecks; PlayerCommands.cpp SetPlayerControl; ObjectCommands.cpp CreateObject/SetObjectVelocity; MissionCleanup.cpp AddEntityToList. MIT citado en NOTICE.md (licencia ya existente).
## Verificación
Primera compilación cliente OK 50.218 s; servidor OK 9.156 s; testbot OK 3.734 s. Pruebas aisladas de funciones reales (ruteo de seis tipos con cuatro clientes simulados, dueño distinto de host, reservas/contador/cancelación/cleanup y WUZI2) pasan; regresión T63 pasa. No ejecutan handlers nativos ni prueban la interacción en GTA. Reconstrucción final y cierre abajo.
SIN REVISAR / SIN PROBAR / SIN INSTALAR. Revisión cruzada de Claude y tres pruebas de juego pendientes. `.git` solo lectura en este entorno: commit a cargo del orquestador.
Commit sugerido: `feat: permitir colocar cargas y lanzar cajas a los invitados`; cuerpo `Adaptado de Karimov-Akbar/sa-coop MissionRules.cpp (MIT). Semántica gta-reversed RunningScript, ObjectCommands y MissionCleanup. Protocolo 0.4.32-alpha.`

## Cierre
- Cliente forzado final OK 32.406 s (`build/t66_client_rebuild_final.log`); servidor forzado OK 7.672 s (`build/t66_server_rebuild_final.log`). Testbot final forzado abajo.
- `tools/test_t66_actions.py`: PASS en funciones reales de servidor/colocación/Catalyst/captura con entorno simulado; `build/t66_unit_final.log`. Regresión T63 PASS (`build/t66_t63_regression.log`); originales verificados (`build/t66_original.log`). No se ejecutó GTA ni testbot de red en vivo para T66.
- READY fiable confirma inicio de secuencia en el propio invitado: sin confirmación en 3 s cancela sin contar; temporización de 6 s empieza al confirmar. Secuencias no interrumpibles, sin bloquear controles del dueño. INPUT de colocación y tiro excluyen foco ajeno, chat y controles desactivados.
- Cambios preparados para revisión y commit del orquestador; `.git` es solo lectura por permisos del entorno. No se instala, juega ni publica. Se liberan reservas #35–37 al entregar.

- Testbot forzado final OK (`build/t66_testbot_rebuild_final.log`). CRLF verificado en 14 archivos C++/headers; archivos nuevos sin espacios finales; `git diff --check` OK. SCM intacto. Reservas de implementación liberadas.
