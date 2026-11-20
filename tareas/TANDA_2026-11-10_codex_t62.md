# T62 — revisión de trenes GLM

Objetivo: verificar y corregir los ocho hallazgos de T62 y commitear el núcleo GLM.
Archivos: los de TANDA_2026-11-10_glm_sync_trenes.md, más client/src/CNetworkVehicle.cpp, client/src/PacketHandlers/peds.cpp, ESTADO.md, INTERRUPTORES.md, TESTEAR_HOY.md (solo inserción al final de Probar ahora), esta tanda y la tanda GLM.
Restricciones: C++ CRLF; sin instalar, sin jugar; preservar cambios ajenos del dueño.
Aceptación: client/server/testbot compilados; testbot TODO PASS; diff limpio; commit solicitado.

Crítica previa: la autoridad de misión cambia durante coop, pero los trenes pertenecen al host de conexión; usar ese mismo criterio en ambos extremos.
Casos borde:
- 3–4 jugadores y entrada tardía: aceptar el primer state independientemente de su secuencia.
- F1 individual y misiones de otro dueño: no cambiar la autoridad global de trenes; manejo remoto queda pendiente.
- Desconexión, muerte o borrado local: sacar ocupantes, limpiar referencias y conservar registros mientras viva el descriptor.
- Interior/otra ciudad: validar vía, nodo, modelos y distancias antes de crear copias.
- Interruptor apagado: no borrar randoms y conservar blips por posición.
- Marcadores personales y scripts originales: sin cambios de reglas ni SCM.

Respondé con un resumen de ≤10 líneas.

## Resultado
- Ocho hallazgos verificados y corregidos; detalle en la sección Revisión 2026-11-10 de la tanda GLM.
- Client -r OK (32.25 s); cambios finales recompilados OK (1.281 s). Server OK; testbot -r OK (2.391 s).
- Testbot canónico: 5 escenarios, 22 chequeos, cero fallas, TODO PASS. Se ejecutó copia del bat con solo rutas de trabajo y filtro de cierre restringidos a build/t62_testserver* dentro del repo por el sandbox; sin modificar el original ni instalar.
- CRLF verificado en C++; revisión cruzada y juego pendientes. TESTEAR_HOY conserva notas del dueño (incluido espacio final preexistente en T17); el commit agrega solo la prueba T62.

## Bloqueo del commit
- Server recompilado completo con -r: OK (6.907 s).
- `git add` falló: `Unable to create .git/index.lock: Permission denied`. El entorno declara .git de solo lectura y no permite elevación; no se creó commit ni se publicó.
- Mensaje solicitado para el commit pendiente: `net: sync de trenes (GLM) + arreglos de revisión (0.4.23-alpha)`. Cuerpo: diseño `_refs/project_gta_sa_coop/docs/design/trains.md` (GPL-3.0); semántica gta-reversed Train.cpp/World.cpp; sa-coop-karimov Trains.cpp (MIT) consultado para limpieza de referencias, sin copiar código. GLM escribió el núcleo, Codex los arreglos de revisión.
- No incluir las ediciones preexistentes de TESTEAR_HOY.md: solo agregar la línea Trenes T62 al final de Probar ahora. T53–T61 y tanda sacoop_karimov quedan fuera.
- Reservas T62 liberadas al cerrar, sin liberar la reserva general #9 que ya existía.
