# T69 — Home Invasion, caja única

Objetivo: ejecutar T69 sobre scripts originales, sin SCM ni instalación. Reserva #40.
Archivos: client/src/CHomeInvasion.{cpp,h}, Commands/CommandHooks.cpp,
CMissionMirror.cpp, CPersistenceSync.cpp (reserva #41), PacketHandlers/{HomeInvasion.cpp,scripts.cpp};
server/src/PacketHandlers/HomeInvasion.cpp; shared/network/packets/home_invasion.h,
shared/network/packet_types.h, shared/config.h; NOTICE.md, ESTADO.md, TESTEAR_HOY.md,
INTERRUPTORES.md, docs/MISIONES_MODO.md, tools/inspect_t69.py y esta tanda.
Restricciones: CRLF C++; no SCM, instalación ni juego. Resumen <=10 líneas.
Aceptación: client/server/testbot; revisión cruzada y juego pendientes.
Protocolo: Codex es único dueño en esta tanda; 0.4.32 -> 0.4.33-alpha si hay paquete.

## Crítica previa
- Caja única resuelve la selección 36@/60@, pero contestar holding no crea una tarea
  de transporte: hace falta arbitraje confirmado y excluir escrituras de ObjectSync.
- GUNS1 escribe el área de la caja según CJ del dueño (BAA62/BAD13), incluso si
  el portador está en el otro lado. Debe redirigirse también esa escritura.
- Caer, cambiar F1 o perder conexión debe invalidar al portador antes de contestar
  otra condición. DELETE_OBJECT puede ocurrir durante WAIT: soltar antes de destruir.
- Ropa/ruido no requieren habilitar respuestas de entrega sin propiedad confirmada.

## Casos borde
- 3–4 jugadores: una caja activa y un portador confirmado; ruido propio por invitado.
- Individual, abandono, muerte/arresto: excluir y restaurar ropa/HUD; fallo colectivo existente.
- Interior distinto: ropa persiste; ruido solo en la casa, aunque el dueño esté afuera.
- Desconexión/fin/reintento: no aceptar ruido de un intento anterior; restaurar ropa propia.
- Apagado conserva T64; no depende de mission-guest-counts.

## Trabajo
**PARCIAL**, por la salida explícita de T69: implementadas ropa y ruido; caja única pendiente.
- CHomeInvasion transmite balaclava de GUNS1 como un cambio de las dos claves especiales,
  conservando el resto del atuendo de cada invitado. Restaura esas claves al quitarla,
  terminar/fallar/abandonar, F1 o perder al dueño/heartbeat. No pisa un atuendo posterior.
  CPersistenceSync excluye la máscara temporal del guardado central.
- Process corre en OnScriptsProcessed, también desconectado; no depende de CAimSync
  ni de mission-guest-counts. Modelo de jugador reemplazado invalida el respaldo viejo.
- Ruido: muestra contador nativo propio en slot libre, casa área 8 y etapa 51@=6/96@=0;
  muestreo 0855, multiplicador 1.6, umbral 20, decaimiento 0.99 por frame. Estado lleno
  del invitado llega al dueño; tras 0090 en BAD5A pone $26820=100 para el chequeo
  nativo BAD66. No cambia NOT, AND/OR ni respuestas de entrega. HUD remoto no pisa
  el valor ni el slot del contador propio. Máximo 600 ms de vigencia de ruido.
- Un paquete HOME_INVASION al final del enum; protocolo 0.4.32 -> 0.4.33-alpha.
  Autoridad CCoopMission, participantes, época por intento y secuencia monotónica;
  heartbeat 200 ms, caducidad 1.5 s; entradas cada 100 ms. Sin requisitos de misma
  área entre dueño e invitado para ropa/estado; ruido solo acepta participante en la casa.
- MIT: adaptados balaclava/UpdateOwnNoise de Karimov-Akbar Missions.cpp; NOTICE actualizado.
  gta-reversed consultado: PedClothesDesc::GetIsWearingBalaclava (modelo especial 9,
  textura 17), PlayerPed::ProcessAnimGroups (wrapper 6098F0, implementación no reescrita
  en esta referencia), Clothes::RebuildPlayer (5A82C0), OnscreenTimer::Add/ClearCounter.

## Qué no quedó resuelto al cierre de T69 (histórico; continuado en T71)
**No se afirma que la caja única sea inviable.** El diseño elimina la cola, pero sigue
faltando la integración de propiedad/tareas nativas con CObjectSync. No se portó un
CrateAnswer que conteste holding sin tener un portador confirmado.
1. CObjectSync::HandleSync reescribe matriz y área de las copias, estáticas y de masa
   infinita. Antes de TakeUp hay que suspender esas escrituras por identidad/época y
   dar al dueño el arbitraje de solicitudes de 2–4 jugadores.
2. El SCM escribe área 0/8 en BAA62/BAD13 según el dueño; el portador puede estar al
   otro lado. Excluir solo ObjectSync no evita esa segunda escritura en el objeto real.
3. BD666 espera dentro de la retirada de la caja: DROP nativo del portador tiene que
   coordinarse con DELETE_OBJECT BD67A y desconexión/F1/fallo, sin punteros colgantes.
   No se implementó ni se habilitó parcialmente ese ciclo; tampoco cola T68.
4. Límite adicional del ruido verificado: fuera de casa GUNS1 pone 94@=0 en BAA88;
   la siguiente vuelta salta desde BA9DA a BC630 y no ejecuta BAD5A/BAD66. La barra
   del invitado sigue siendo propia, pero **despertar con dueño afuera está pendiente**.
   No se fuerza 94@ ni el área del dueño para alcanzar el bloque: afectaría caja/HUD.

## Verificación
- `python -I tools/inspect_t69.py`: PASS huella DA1FBDD4, 0090 exacto, comparación,
  puertas de ruido fuera de casa y ciclo de área/WAIT/borrado de caja. Solo evidencia
  del original, no prueba de comportamiento. No escribe SCM.
- Client reconstrucción completa OK 58.188 s; server OK 13.219 s; testbot OK 4.625 s.
  Último ajuste de limpieza/HUD recompilado al cierre en build/t69_client_close.log.
- Cuatro pruebas manuales actualizadas en TESTEAR_HOY; la primera marcada como no
  implementada, ruido con dueño afuera también pendiente. No se ejecuta el juego.
- SIN REVISAR / SIN PROBAR / SIN INSTALAR. No SCM ni publicación. Sin commit porque
  .git es solo lectura en este entorno. Reservas #40/#41 y #42 (Main.cpp no usado).


Cierre: client incremental final OK 2.547 s tras mover Process fuera del gate de autenticación.

## Continuación T71 (2026-11-10)
Los puntos 1–3 se implementaron en la tanda codex_t71: reserva confirmada por identidad/época, exclusión de ObjectSync, tarea nativa, corrección de área post-SCM y drop antes de destruir. Punto 4: límite aceptado, sin forzar 94@. Protocolo 0.4.35-alpha. SIN REVISAR / SIN PROBAR / SIN INSTALAR; ver prueba actual en TESTEAR_HOY.md.

## Continuacion T74 (reemplaza el diseno de ruido anterior)
Una sola barra: aportes parciales acumulativos de invitados a 167@ en BAD5A,
valor/visibilidad por HUD espejo existente. Sin contador propio ni aviso booleano lleno.
94@=0 descarta aportes; no mide con dueno afuera ni fuerza SCM.
Balaclava y cajas conservadas. Protocolo 0.4.36-alpha; ver tanda T74.
