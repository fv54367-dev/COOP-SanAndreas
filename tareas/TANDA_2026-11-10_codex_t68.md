# T68 - revision de serializacion Home Invasion

Objetivo: ejecutar T68 con scripts originales; si la serializacion no resulta limpia, detener y explicar conforme a la tarea.
Archivos reservados (#39): esta tanda, tareas/TANDA_2026-11-10_codex_t64.md, tools/inspect_t68.py, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md.
Restricciones: sin SCM, sin instalaci-n ni juego; CRLF si se modifica C++. Resumen <=10 l-neas.
Aceptacion de implementacion: client/server/testbot, propiedad exclusiva, entrega unica y cuatro pruebas manuales. Alternativa explicita T68: detener con evidencia del impedimento de serializar limpiamente.

## Critica previa y casos borde
- El LOCATE de entrega no se ejecuta si 60@ no vale 1; no basta ampliar su respuesta con T63.
- Con 3-4 jugadores, mantener estado por caja y portador; nunca usar la caja del dueno como estado provisional durante una espera.
- F1 individual y abandono excluyen participaci-n; desconexion suelta; muerte/arresto conserva fallo colectivo.
- Portadores en areas distintas necesitan estado propio de area, independiente del cruce del dueno.
- Fin/reintento/carga deben cancelar entregas, liberar referencias y restaurar ropa/HUD propios.
- Apagado conserva T64. No cambiar protocolo para un dise-o detenido.

## Resultado: DETENIDA, sin implementacion de juego
Se aplica la salida explicita de T68: no forzar una serializacion que no queda resuelta con los puntos de inyeccion indicados. No se afirma que sea imposible desarrollar una maquina de estados C++ adicional.

Evidencia del original, mision 24, inicio B69A0, longitud 41159, FNV DA1FBDD4:
1. Antes de BD1DE/BD264, BD1B0 exige 60@=1 y BD1B7 exige 51@=6. Due-o sin caja: 60@=0 y el script salta a BD779. La cola no puede seleccionarse justo en BD1DE porque ese comando nunca se alcanza.
2. Si se selecciona al invitado antes de esa puerta, BD119 pregunta por el CJ del dueno y BD133/BD13E liberan 61[36] y 60 cuando CJ no sostiene la caja. Hay que coordinar selecci-n con este bloque, no solo con la entrega.
3. BD5C9 pone 60@=0; BD607/BD60E pisan 52@/33@ y BD666 hace WAIT 0 dentro del borrado, con 36@ todavia activo. No hay una transaccion atomica hasta BD697. Restaurar 36@ durante esa espera permite que BD67A borre la caja del dueno; restaurarlo antes de BD769 puede tambien afectar el tramo posterior. Hace falta un estado de entrega en vuelo, puntos de salida/cancelacion y restauracion de los estados auxiliares, no unicamente 36@/60@.
4. BAA62/BAD13 cambian el area de la caja activa por el cruce del dueno. CObjectSync solo tiene autoridad de mision: sus copias son estaticas, masa infinita, fuerza desactivada y cada ObjectSync vuelve a escribir matriz/area. No existe a-n propiedad de caja por invitado ni exclusion de esa escritura mientras la sostiene. A-adir solo una consulta T63 dejar-a dos escritores de la misma caja.

La sospecha inicial de que la primera entrega jugable evita el borrado fue DESCARTADA: B9A52 incrementa 35@ y B9A65 borra la primera caja durante la secuencia inicial. No se usa como bloqueo.

Para continuar limpiamente: primero implementar arbitraje/portador por identidad y epoch sobre CObjectSync; despues selecci-n antes de BD1B0, reserva persistente a trav-s de WAIT, confirmaci-n de soltar la copia y restauracion por salida normal/cancelacion/fin. Debe preservar 61[], blip 104@ y estados 55@/56@/75@ junto a 36@/60@, sin restaurar contadores legitimos de entrega. Esta maquina todavia NO esta implementada ni validada; no se habilitan parcialmente ropa o ruido dejando la entrega aparentando funcionar.

Referencias consultadas: Karimov-Akbar/sa-coop (MIT), Objects.cpp Held/TakeUp/HandleHold/FollowHolder y Missions.cpp CrateAnswer/ApplyMissionLook/UpdateOwnNoise (licencia ya en NOTICE.md). gta-reversed Entity/Ped/Ped.cpp GetHoldingTask/DropEntityThatThisPedIsHolding (5E0360): soltar usa la tarea nativa y borrar una entidad de mision no equivale a terminar la tarea del portador. Ningun codigo de referencia copiado.

Verificacion reproducible: python -I tools/inspect_t68.py verifica huella y muestra puertas, WAIT, area y borrado directamente del SCM original, sin escribirlo. Las cuatro pruebas de juego siguen pendientes, no completadas. SIN REVISAR / SIN PROBAR / SIN INSTALAR. Compilacion base se registra al cierre; no demuestra T68.

## Cierre
- Rebuild client OK 39.078 s; server OK 8.375 s; testbot OK (build/t68_{client,server,testbot}.log). Es la base sin cambios C++, no implementacion T68.
- inspect_t68.py PASS: huella original, puertas, WAIT y cambios de area. git diff --check OK.
- Sin cambios C++/protocolo/SCM ni instalacion. Cuatro pruebas manuales siguen pendientes. Sin commit: .git solo lectura en este entorno. Reserva #39 liberada al cierre.
