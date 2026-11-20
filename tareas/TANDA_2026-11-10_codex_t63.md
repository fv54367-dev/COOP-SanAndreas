# T63 - lo que hace el invitado cuenta
Objetivo: consultas genericas y lineas seguras en C++, sin SCM ni instalacion.
Archivos: client/src/CMissionGuestCounts.{cpp,h}, CMissionSyncRules.cpp, CAimSync.cpp, COpCodeSync.{cpp,h}, Commands/CommandHooks.{cpp,h}; client/src/PacketHandlers/MissionGuestCounts.cpp, server/src/PacketHandlers/MissionGuestCounts.cpp, shared/network/packets/mission_guest_counts.h, shared/network/packet_types.h, shared/config.h, client/src/CConfigLoader.{cpp,h}, tools/test_t63_conditions.py, NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md y esta tanda.
Reservas #25-28; compartidos con T60 coordinados en Reparto de TANDA_2026-11-10_sacoop_karimov.md. No tocar sus handlers/Main.cpp/testbot.cpp. Protocolo T63: 0.4.28 -> 0.4.29-alpha; MISSION_GUEST_COUNTS al final.
Restricciones: CRLF, sin SCM, sin instalar/jugar. Resumen <=10 lineas. SIN REVISAR / SIN PROBAR / SIN INSTALAR.
Critica previa: NOT despues de agregar respuestas; restaurar AND/OR, IP, parametros y captura de red. T46 ya aplica el dano: no generar eventos. Apuntado explicito, no inferido por camara. Offsets solos no prueban un script original.
Casos borde:
- 3-4 jugadores: OR de participantes vivos, copias validas y misma area; individuales/tardios/salidos excluidos.
- Desconexion/muerte/abandono: filtro de participantes, estado caduca en 600 ms, sin punteros de entidades guardados.
- Reto/epoch por intento y dueno; SCRIPT_NAME invalida reintentos que reutilizan slot.
- Marcadores de inicio y espera de todos intactos; solo spots enumerados evitan el filtro de espera.
- Scripts modificados: genericas por parametro; spots solo con huella de codigo original coincidente.
- mission-guest-counts=true; apagado conserva condiciones anteriores y no envia/acepta estado nuevo.
- Tramos largos/Catalyst fase 2: necesitan redirigir animaciones/controles/entidades.
Aceptacion: client/server/testbot compilados; CRLF, pruebas aisladas/regresion y docs manuales; cita MIT. Juego pendiente del dueno.

## Implementación y líneas portadas
Referencia MIT: Karimov-Akbar/sa-coop `features/story/mission-rules/MissionRules.cpp` (`Ask`, `Answer`, `kSpots`); conexión de `rules::Answer` en `features/story/missions/Missions.cpp:1826`. Adaptado al dueño de misión y filtros locales; licencia completa en NOTICE.md.

Genéricas: `051A HAS_CHAR_BEEN_DAMAGED_BY_CHAR`, `051C HAS_CAR_BEEN_DAMAGED_BY_CHAR`, `02E0 IS_CHAR_SHOOTING`, `02D6 IS_CHAR_SHOOTING_IN_AREA`, `0457 IS_PLAYER_TARGETTING_CHAR`. Resultado crudo ampliado antes de NOT/AND/OR. Reconsulta nativa sobre una copia del script con parámetros inmediatos: cubre aliases del actor sin escribir `$PLAYER_ACTOR`. Restaura los 32 ScriptParams, los 15 parámetros de red y sus dos contadores; sin esferas extra.

| Misión | Inicio / línea original | Opcode | Continuación verificada |
|---|---|---|---|
| Black Project / MAF4 | 1E9490 / **1ECB9D** | 0102 | Activa 182@; puerta deslizante, blip, audio y temporizador. No controla a CJ. Bloque 30721 bytes, FNV 5B4C70DF. |
| Green Goo / DES10 | 1F0C91 / **1F1A67** | 00FE | Borra frasco, cambia blip/contadores. Regreso/cinemática conserva LOCATE independiente 1F1AF7. Bloque 4603 bytes, FNV F4F9C0B1. |
| Vertical Bird / MANSIO2 | 25B5E6 / **2614F8**, **264BA2** | 09ED | Detección cambia 73@, rutas de guardias/alarma. Bloque 53511 bytes, FNV 14481C7C. |

Spots requieren nombre, opcode, offset y FNV-1a de todo el bloque original coincidentes; huella recalculada al consultar, sin caché que sobreviva recargas. Offsets/continuaciones desensamblados del main.scm original local (3.079.599 bytes), solo lectura, parser de referencia y base de opcodes del SDK local.
Semántica contrastada con gta-reversed `Scripts/RunningScript.cpp::UpdateCompareFlag`, `Scripts/Commands/CharacterCommands.cpp::{HasCharBeenDamagedByChar,IsCharShooting,IsCharShootingInArea}` y `PlayerCommands.cpp::IsPlayerTargettingChar`.

Excluidas por seguridad: SYND4 **18DE78/18DE9E** activa teléfono, limpia tareas y pasa por subrutinas de control del dueño; DES3 **1DDDA1**: contrabando → SET_PLAYER_CONTROL y SET_CHAR_COORDINATES del dueño.
**Fase 2:** todos los spots con `until`: MAF4 1EED91/1EF0FD, DRIV2 19E695, HEIST2 23B69E, WUZI2 16ED89, HEIST4 23E4C2 y MANSIO2 261287/2647C7; Deconstruction y Catalyst/Throw. Requieren redirigir animaciones, controles o entidades.

## Verificación final
- Cliente completo `xmake build -r client`: OK, 37,969 s (`build/t63_client_complete.log`). Servidor OK, 15,016 s (`build/t63_server.log`); testbot OK, 2,813 s (`build/t63_testbot.log`).
- Regresión ENet real en servidores aislados bajo `build/t63_testserver*`, puertos 6813/6814: 8 escenarios / 69 chequeos / cero fallas (`build/t63_regression.log`); servidores cerrados al finalizar. No instala ni abre GTA.
- `python tools/test_t63_conditions.py`: OK. Verifica cuatro offsets/opcodes/huellas del original local; ejecuta la función real ProbeCondition con handlers simulados en 20 combinaciones NOT/AND/OR y verifica restauración de IP/locales/parámetros/captura; ejecuta el handler real del servidor con red simulada para cuatro jugadores, dueño distinto del host, sellado de remitente y rechazos de individual/no participante/misión terminada/destino inválido/epoch inválido. **No prueba handlers nativos de GTA ni la interacción nueva de punta a punta en juego.** Log `build/t63_unit.log`.
- CRLF en los 15 archivos C++/headers tocados y `git diff --check`: OK. Sin SCM, instalación, juego ni publicación.
- Revisión propia corrigió que los spots también eviten la espera grupal cuando la condición ya es verdadera para el dueño; los demás LOCATE conservan las reglas previas. Las consultas genéricas también cubren secundarias sincronizadas; se siguen excluyendo misiones individuales y participantes ajenos.
- Pendiente: revisión cruzada de Claude y pruebas de TESTEAR_HOY.md. Tramos largos/Catalyst documentados como fase 2.
- Sin commit: permisos del entorno permiten **solo lectura de .git** y prohíben elevación. Cambios listos para revisar/commitear; reservas liberadas para no bloquear al orquestador.
- Commit sugerido: `feat: contar acciones de invitados en las misiones`. Cuerpo: `Portado de sa-coop (Karimov-Akbar, MIT): MissionRules.cpp (Ask/Answer y spots conservadores). Semántica contrastada con gta-reversed RunningScript::UpdateCompareFlag, CharacterCommands y PlayerCommands. Protocolo 0.4.29-alpha.`
