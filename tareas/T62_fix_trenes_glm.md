# Tarea T62: arreglar la revisión de los trenes de GLM y commitearlos
Leé `tareas\COMUN_reglas.md`, `AGENTS.md` y `tareas\TANDA_2026-11-10_glm_sync_trenes.md`. El árbol tiene el trabajo de GLM sin commitear (`client/src/CTrainSync.*`, `PacketHandlers/train.cpp` de cliente y server, `shared/network/packets/train.h` y ediciones). Compila y el testbot pasa. **No toques `TESTEAR_HOY.md`** (tiene notas del dueño).

## Hallazgos de la revisión (verificá cada uno antes de arreglarlo; si uno es falso, decilo y no lo toques)
1. `CTrainSync.cpp` ~164 `GetConfigModels`: la tabla de configuraciones en 0x8D44F8 sería de `uint32_t`, no `uint16_t` (mirá los bytes en el exe con `pefile` y `_refs\gta-reversed` `Train.cpp`); con `uint16_t` corta después de la locomotora.
2. ~458 `RemoveCopy` borra los registros que `CreateCopy` (~405) necesita para reconstruir tras un descarrilamiento o borrado local: la copia nunca vuelve.
3. `server/src/PacketHandlers/train.cpp` ~14 valida `IsScriptAuthority`, pero el emisor manda si `m_bIsHost` (~632): cuando la misión coop es de otro jugador, el server rechaza los states. Unificá el criterio (uno solo, el mismo en los dos lados).
4. ~665 `OnBecameFollower` borra trenes random sin mirar `train-sync`: con el interruptor apagado tiene que ser exactamente lo de antes.
5. Al borrar copias falta `CWorld::RemoveReferencesToDeletedObject` (o el camino del juego equivalente) antes de liberar el vagón: un jugador parado arriba queda con referencias colgantes. Mirá cómo lo hace `_refs\sa-coop-karimov\src\client\features\trains\Trains.cpp` (MIT).
6. ~766 el snapshot pone `bHasState` con `lastSeq=0`: el que entra con la secuencia del host entre 128 y 255 descarta los states nuevos (~818). Que el snapshot traiga la secuencia o que el primer state se acepte siempre.
7. `client/src/CMirrorBlips.cpp` ~284: la condición quedó invertida. Con `train-sync=false` el blip de tren tiene que seguir el camino de antes (solo posición); con `true`, por identidad del vagón.
8. ~740 el descriptor recibido no se valida: `config` en rango, modelos que sean trenes, nodo válido y distancias finitas antes de llamar al juego.
9. (Limitación, no arreglar acá) el invitado en la cabina no puede manejar: anotalo como fase siguiente en la tanda de GLM (su `TRAIN_DRIVE`/`TRAIN_ENTER` es la referencia).

## Aceptación
Compilan client/server/testbot y `tools\correr_testbot.bat` da TODO PASS. CRLF. Commit con mensaje `net: sync de trenes (GLM) + arreglos de revisión (0.4.23-alpha)`, citando `_refs\project_gta_sa_coop\docs\design\trains.md` (GPL-3.0) y sa-coop (MIT) si portás algo. Agregá en la tanda de GLM una sección "Revisión 2026-11-10" con qué se arregló. En `INTERRUPTORES.md`, `train-sync` (si no está). Una prueba simple en `TESTEAR_HOY.md` **solo agregando líneas al final de "Probar ahora"**. No instales. Resumen ≤10 líneas.
