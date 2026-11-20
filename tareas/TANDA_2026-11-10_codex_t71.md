# T71 — transporte de caja GUNS1

Objetivo: completar T71 sin SCM ni instalación. Codex, único dueño del protocolo.
Archivos: client/src/CHomeInvasion.{cpp,h}, CObjectSync.cpp, CMissionSyncRules.cpp;
shared/network/packets/home_invasion.h, shared/config.h; NOTICE.md, ESTADO.md,
TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md,
tareas/TANDA_2026-11-10_codex_t69.md, tools/test_t71.py y esta tanda.
Restricciones: CRLF C++; sin SCM, instalación ni juego; revisión cruzada pendiente.
Aceptación: client/server/testbot compilan; regresión testbot; resumen <=10 líneas.
Protocolo: extender HOME_INVASION existente, 0.4.34 -> 0.4.35-alpha.

## Crítica previa y casos borde
- La referencia levanta antes de arbitrar: acá la tarea local espera confirmación.
- Dos invitados y dueño: una reserva global, primero confirmado; bloquear liftable.
- 3–4: snapshots identifican portador; espectadores siguen la misma caja.
- F1/muerte/desconexión/fallo: soltar por identidad, sin guardar punteros de entidades.
- Área del script: corregir después de BAA62/BAD13, sin mover al dueño.
- DELETE_OBJECT y limpieza de copias: drop antes de destruir; reintento usa época nueva.
- Apagado conserva T69; ruido con dueño afuera es limitación aceptada.

## Resultado
Implementado, SIN REVISAR / SIN PROBAR / SIN INSTALAR.
- HOME_INVASION ampliado (sin enum nuevo), protocolo 0.4.35-alpha. Snapshot con
  siete identidades, liftability y portador; petición/botón/holding con época y secuencia.
- El dueño confirma una sola reserva antes de la tarea del invitado. Bloquea otras
  cajas; su propia recogida nativa ya iniciada tiene prioridad. Solicitud requiere
  participante elegible, caja autorizada, misma área y distancia menor a 2.5 m.
- Held/TakeUp/FollowHolder adaptados de Karimov-Akbar Objects.cpp, MIT (NOTICE).
  Snapshot inicia tarea nativa en portador/espectadores. El dueño sigue su copia
  remota; fallback a mano después de 2 s si esa copia está ocupada.
- CObjectSync excluye matriz/área mientras hay reserva; DestroyCopy suelta antes
  de destruir, también al limpiar/reemplazar. Identidades resueltas cada vez, sin
  guardar punteros de caja/ped. Revocar cancela recogida pendiente además de DROP.
- Post BAA62/BAD13 corrige el área según portador. Pre BD67A libera antes del
  borrado; el ObjectRemove existente destruye las copias. Timeout de entrada 1.5 s,
  aproximación fallida 3 s, F1/muerte/desconexión/fallo liberan donde estaba portador.
- CrateAnswer pasa por FilterCompareFlag/UpdateCompareFlag de T63, independiente
  de su interruptor. 0737/0475 solo para caja reservada; furgoneta por firma exacta
  kGunsTruck en BD1DE, botón BD264 por portador con vigencia 1 s. No mueve al dueño.
- Ruido con dueño afuera: limitación aceptada, documentada como resultado esperado.

## Verificación
- `python -I tools/test_t71.py`: PASS firma original y ciclo área/WAIT/borrado;
  receptor C++ de producción compilado con nativos simulados: dos pretendientes,
  prioridad de recogida del dueño, secuencia/época viejas, participante excluido,
  área/distancia/identidad incorrectas y confirmación falsificada.
- Server 10.312 s y testbot 3.718 s OK. Cliente recompilado tras cada ajuste;
  reconstrucción completa final OK, 58.765 s, registrada en build/t71_client_rebuild.log.
- Runner canónico ../tools/correr_testbot.bat copiado temporalmente en build con
  SOLO las carpetas _testserver reubicadas dentro de build (sandbox solo permite
  escribir dentro del repo). Misma lógica/puertos/escenarios: 9 escenarios/90
  chequeos y (d) 3 chequeos, cero fallas, TODO PASS (build/t71_regression.log).
- CRLF C++ y diff --check verificados al cierre. Documentos requeridos actualizados.
- Falta revisión cruzada de Claude y pruebas manuales TESTEAR_HOY. No se ejecutó
  GTA ni se instaló. No commit: .git es solo lectura en este entorno.

