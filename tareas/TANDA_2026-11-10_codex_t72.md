# T72 — robo nativo entre jugadores
Objetivo: ejecutar tareas/T72_robo_auto_entre_jugadores.md sin instalar ni jugar.
Archivos reservados: client/src/CJackSync.{cpp,h}, Hooks/{TaskHooks,PlayerHooks}.cpp,
CPassengerEnter.cpp, CNetworkVehicleManager.cpp, CNetworkPlayer.cpp,
PacketHandlers/{players,vehicles}.cpp; ESTADO.md, TESTEAR_HOY.md,
INTERRUPTORES.md, TANDA_2026-11-10_sync_visual.md y esta tanda.
Restricciones: CRLF C++; sin protocolo, SCM, instalaciones ni cambios del server.
Aceptación: client/server/testbot compilan, runner testbot TODO PASS; resumen <=10 líneas.
Casos borde:
- 3–4: observadores dejan terminar su tarea, sin aplicar salida/posición ajena antes.
- F1 individual y misión coop: robo físico independiente, sin disparar fallo de misión.
- Víctima desconectada o ladrón muerto: sin punteros guardados; límite de recuperación.
- Otra área/lejos: no crear víctima ni moverla para reproducir el robo.
- Conductor/pasajero: aviso al arrastre efectivo, no durante aproximación cancelable.
- Moto/bici: usar tarea BikeJacked; apagado mantiene camino anterior a T51.
Estado: implementado; SIN REVISAR / SIN PROBAR / SIN INSTALAR.


## Verificación y entrega
- Cliente: compilación incremental recompiló los ocho .cpp tocados; último ajuste
  CJackSync.cpp recompilado/enlazado OK (2.687 s).
- Server reconstruido -r OK (16.297 s); testbot -r OK (5.609 s).
- Runner canónico ../tools/correr_testbot.bat copiado en build/t72_testbot.bat,
  cambiando SOLO SRV1/SRV2 para escribir dentro del repo permitido.
  build/t72_regression.log: 9 escenarios/90 chequeos + d/3 chequeos, cero fallas,
  TODO PASS. Esto no prueba la animación del cliente.
- CRLF de los ocho .cpp y CJackSync.h comprobado; git diff --check OK.
- Evidencia con archivo:línea en sección T72 de TANDA_2026-11-10_sync_visual.md;
  ESTADO, TESTEAR_HOY e INTERRUPTORES actualizados.
- Sin juego, instalación ni publicación. Falta revisión cruzada de Claude y
  prueba visual del dueño. Sin commit: .git es solo lectura en este entorno.
