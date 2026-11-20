# Tarea T80: trenes duplicados, copias huérfanas en el seguidor (T76 no alcanzó)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md` (hipótesis T80) y `tareas\T76_trenes_duplicados.md`. Código: `client/src/CTrainSync.*`, `client/src/PacketHandlers/vehicles.cpp` (remove de vagones), `client/src/CNetworkVehicle*.cpp` (DestroyGameVehicle, stream out). No toques `main.scm` ni `scm/`.

## Qué reportó el dueño (después de T76)
Siguen apareciendo varios trenes: en el mismo riel, uno viene de la derecha y otro de la izquierda; "se duplican literal". De vez en cuando aparecen algunos quietos en el medio de la nada. El log del invitado no tiene ni un `train-local-random`.

## Qué hacer
1. **Logs:** todo `logger::info("train-sync: ...")` del client se pierde (stdout). Mandalos también al mirror log (`CMissionMirror::LogEventf`, prefijo `train-`), sin spam por frame.
2. **Confirmar o descartar la hipótesis de Claude** (en la tanda) leyendo el código: dónde puede quedar en el mundo una cadena creada por `CreateCopy` que ya no está ligada (`RemoveCopy` con registro nulo o `m_bSyncing`, `FollowerWatchdog`, `HandleSpawn` con descriptor distinto, `OnCarriageRecordRemoved`, stream out de un vagón suelto, reconexión, cambio de rol). Citá gta-reversed `Train.cpp` (`CreateMissionTrain`, `RemoveOneMissionTrain`, `ProcessControl`).
3. **Arreglo de raíz:**
   - `RemoveCopy` saca la cadena entera recorriendo desde `pHead` (validando punteros vivos en el pool), no solo los vagones con registro.
   - Un barrido en el seguidor: toda cadena que creó `CreateCopy` y ya no está ligada a un `STrainMirror` se borra. Llevá registro de las cabezas que creaste y no toques trenes de misión del script local.
   - Log `train-orphan-removed` con id, posición y motivo.
4. Revisá también el host: ¿puede quedar un tren registrado dos veces o registrarse una cadena que ya era copia? Si encontrás algo, arreglalo y documentalo.

## Casos borde
- Jugador subido a la copia que se borra: bajarlo sin crash, igual que `DestroyGameVehicle`.
- 3–4 jugadores.
- Host que cambia.
- `train-sync=false`: igual que antes.

## Aceptación
Compilan client/server/testbot, `tools\correr_testbot.bat` TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (trenes) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
