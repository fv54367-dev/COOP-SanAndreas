# T74 - ruido compartido Home Invasion
Objetivo: aportes de invitados al acumulador nativo 167@ del dueno; HUD espejo existente.
Archivos: client/src/CHomeInvasion.{cpp,h}, client/src/PacketHandlers/scripts.cpp,
shared/network/packets/home_invasion.h, shared/config.h, TESTEAR_HOY.md,
ESTADO.md, INTERRUPTORES.md, docs/REQUISITOS.md (reserva #46), tareas/TANDA_2026-11-10_codex_t69.md,
tools/test_t74.py y esta tanda. Reserva #45. Codex unico dueno del protocolo.
Restricciones: CRLF C++; sin SCM, instalacion ni juego. Resumen <=10 lineas.
Aceptacion: client/server/testbot y runner canonico TODO PASS.
## Critica previa / casos borde
- Un booleano lleno pierde los aportes parciales: transmitir total acumulativo por intento,
  deduplicar secuencia y consumir diferencias una vez; no repetir ruido al perder paquetes.
- 3-4 suman; individual/abandono/muerte/desconexion excluidos por elegibilidad existente.
- 94@=0 o fase inactiva: descartar aportes, sin forzar ruta SCM ni acumular para reingreso.
- Interior/posicion fuera de casa excluidos; fin/reintento reinicia total con epoca nueva.
- El espejo existente debe controlar valor/visibilidad para todos, incluso invitados afuera.
- Apagado conserva comportamiento anterior al sistema coop-home-invasion.
Protocolo: 0.4.35 -> 0.4.36-alpha, HOME_INVASION existente.

## Resultado y verificacion
- Implementado: total acumulativo por invitado/epoca en HOME_INVASION; diferencias
  deduplicadas consumidas una vez sobre 167@ y $26820 en BAD5A, antes de BAD66.
- Eliminado contador propio y exclusion del espejo; el HUD nativo del dueno gobierna todos.
- 94@=0/fase inactiva descarta pendientes; elegibilidad/casa/vigencia 600 ms.
- Balaclava/cajas conservadas, sin SCM ni instalacion. Protocolo 0.4.36-alpha.
- Referencias: original huella DA1FBDD4 (167@, BAD50/BAD5A/BAD66, 94@);
  gta-reversed Events/EventGlobalGroup.cpp GetSoundLevel (0x4AB900) y Event.cpp (0x4B2850).
- python -I tools/test_t74.py PASS: receptor de produccion (replay/perdida,
  aportes parciales, exclusiones, 94@=0 y arbitraje de cajas T71); cuerpo de
  acumulacion real con nativos simulados (3 invitados, umbral y consumo unico).
- Server OK 8.828 s; testbot OK 3.187 s. Runner canonico copiado a build,
  solo carpetas _testserver reubicadas dentro del repo por sandbox: 9 escenarios/90
  chequeos + (d) 3 chequeos, cero fallas, TODO PASS (build/t74_regression.log).
- SIN REVISAR / SIN PROBAR / SIN INSTALAR. Falta revision cruzada y juego.
- Sin commit: .git es solo lectura en este entorno; reservas #45/#46 quedan con vencimiento.

Cliente reconstruccion completa -r OK 62.422 s (build/t74_client_rebuild.log). CRLF y git diff --check OK.
