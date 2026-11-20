# T80 — copias huérfanas de trenes

Objetivo: borrar completamente las cadenas creadas por CreateCopy aunque pierdan registros o cambien de dueño.
Archivos reservados: `client/src/CTrainSync.cpp`, `ESTADO.md`, `TESTEAR_HOY.md`, esta tanda.
Restricciones: CRLF C++, sin SCM, protocolo, instalación ni juego. Interruptor existente train-sync.
Aceptación: client/server/testbot compilados, regresión TODO PASS, diff y CRLF verificados.
Casos borde:
- 3–4 jugadores: registro de copias local independiente; F1 e interiores no cambian su propiedad.
- Desconexión/reconexión/cambio de host: limpiar copias antes del nuevo escaneo, conservar simulación propia.
- Vagón sin registro, sincronizando o separado y cabeza destruida: conservar referencias del pool de toda la creación.
- Jugadores dentro/sobre el tren: salida nativa y limpieza de referencias antes de borrar.
- No borrar trenes de misión locales ni entidades nuevas que reutilicen una dirección del pool.
- train-sync=false: limpieza de nuestras copias y comportamiento anterior.

Evidencia: RemoveCopy omitía registros nulos/m_bSyncing y olvidaba pHead; Watchdog reconstruía después.
StreamOut destruye un vagón aislado; la cola superviviente deja de estar alcanzable desde la cabeza.
HandleSpawn reemplazaba descriptor; OnCarriageRecordRemoved y FreeAll usaban la misma limpieza incompleta.
Host: IsAnyCarriageRegistered dependía de registros y HandleSpawn no rechazaba eventos siendo host.
Referencia: gta-reversed Train.cpp RemoveOneMissionTrain 249–262 recorre enlaces sin evacuar ocupantes;
CreateMissionTrain 407 delega al exe 0x6F7550; ProcessControl 422+ sigue moviendo vagones sin espejo.

Implementado: registro independiente de cabeza y todos los vagones mediante handles del pool, recorrido validado
de la cadena y borrado de supervivientes separados, incluso sin registro o con m_bSyncing. Salida nativa de
ocupantes y limpieza de referencias al techo; eliminación del conductor cosmético. Barrido de creaciones sin
espejo y limpieza en FreeAll para cambio de rol/desconexión. Fallos de creación también limpian su cadena.
Watchdog valida identidad y enlaces; no considera m_bSyncing una pérdida por sí solo. No se borran entidades
vivas ajenas al registro de creaciones al olvidar identidades. Eventos de spawn/remove ignorados por el host;
deduplicación adicional por handle de cabeza. Todos los logs train-sync llegan también al mirror;
train-orphan-removed incluye id, posición y motivo (incluye retirada normal). Creaciones fallidas reintentan
cada 3 s; configuración desconocida del host limita el log a ese mismo intervalo.

Verificación: client compilado con -r (52,782 s) y cambios finales recompilados explícitamente;
server -r (10,407 s), testbot -r (3,844 s), todos OK. Regresión: 11 escenarios / 101 chequeos / 0 fallas,
TODO PASS. Se ejecutó copia de tools/correr_testbot.bat en build/t80_testbot.bat: únicamente carpetas de
servidores temporales dentro del repo y cierre limitado a sus dos rutas exactas (restricción del sandbox).
CRLF CTrainSync.cpp y git diff --check OK. ESTADO y TESTEAR_HOY actualizados. Sin SCM, protocolo ni instalación.
Revisión cruzada de Claude y prueba en GTA pendientes; el testbot no valida la limpieza del cliente.
Sin commit: .git es solo lectura en esta sesión; reserva #53 conservada hasta que el orquestador lo haga.
Respondé con un resumen de ≤10 líneas.
