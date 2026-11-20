# T55 — Billar de a dos
Objetivo: POOL2 nativo con segundo pad por red, sin SCM ni instalación.
Archivos: client/src/CCoopPool.{cpp,h}, CNetworkPadSync.cpp, COpCodeSync.cpp, CMissionSyncRules.cpp, CCameraMirror.cpp, Main.cpp, CConfigLoader.{cpp,h}, PacketHandlers/Scripts.cpp; server/src/PacketHandlers/Scripts.cpp; shared/network/packets/scripts.h, packet_types.h, shared/config.h; ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, NOTICE.md, docs/SISTEMAS.md y ambas tandas T55/Karimov.
Ampliación de archivos: client/src/CMissionMirror.cpp (reset al recargar mundo), tools/testbot/testbot.cpp (escenario de protocolo con cuatro clientes). Reservas #16 y #17.
Restricciones: CRLF, MIT citado, dueño único del protocolo 0.4.26-alpha; sin instalación ni juego.
Crítica previa: el intento anterior elegía el primer jugador sin consentimiento, no transportaba mouse ni espejaba bolas. No usar autoridad global de historia para un minijuego local. Separar ofertas y sesiones; rechazar mensajes atrasados y otros interiores. El START nativo devuelve la apuesta: conservar ese camino y no agregar liquidación compartida.
Casos borde:
- 3–4 jugadores: primer JOIN aceptado ocupa el segundo pad; los demás solo observan la mesa.
- F1 individual, opción apagada o distancia >8 m/otra área: no unirse ni recibir réplica.
- Desconexión, muerte, arresto o salida: cancelar por salida nativa, restaurar control/cámara y borrar objetos propios.
- Sin apuesta para dos; nativo devuelve el depósito del dueño al entrar por START, dinero remoto intacto.
- Ofertas caducan; sesión e ids fijados por servidor; no aceptar INPUT de espectadores.
Aceptación: compilan client/server/testbot, diff y CRLF verificados; revisión cruzada y prueba del usuario pendientes. Resumen ≤10 líneas.


## Implementación — Codex, SIN REVISAR / SIN PROBAR / SIN INSTALAR
- `pool-two-players=true`, consentimiento F7 cercano en apuesta, sesión independiente del host de historia, segundo pad/mouse y turno nativo. Protocolo 0.4.26-alpha, COOP_POOL al final.
- VIEW de mesa existente, hasta 75 props con interpolación, cámara y avatar/taco nativos; tercero/cuarto espectadores. RESULT del nativo. Sin apuesta: START devuelve depósito del dueño; dinero ajeno intacto.
- Limpieza por F7/desconexión/individual/muerte, timeout 5 s, salida nativa 108@=1 y reset de handles al recargar. Referencia MIT Pool.cpp, licencia en NOTICE.md; semántica gta-reversed RunningScript/PadCommands/RpAnimBlend/Ped.
- Crítica: viejo CNetworkPadSync elegía primer remoto sin consentimiento ni mouse/mesa. Se conserva solo apagado; nuevo módulo excluye POOL2 del espejo genérico para evitar duplicación.
- Testbot f con cuatro clientes cubre consentimiento, remitente, tercer jugador, individual/lejanía, sesión vieja, snapshot máximo y liberación; resultados finales en tanda T55.


## Verificación y entrega
- Client completo `-r`: OK final (33.969 s), incluido bloqueo de mesa para espectadores.
- Server completo `-r`: OK (6.781 s); testbot completo `-r`: OK (2.844 s).
- Runner canónico adaptado solo en `build/t55_run.bat` para limitar escritura/cierre a `build/t55_testserver*`: 6 escenarios, 38 chequeos, cero fallas, TODO PASS. El escenario f agrega 16 comprobaciones con 4 clientes: no reemplaza prueba visual.
- Revisado contra gta-reversed: pad/mouse, compare NOT/AND/OR, inicialización de clumps antes de leer asociaciones, DropEntityThatThisPedIsHolding(false); salida 108@ en POOL2 conserva cleanup sin saltar IP.
- Sin SCM, instalación, ejecución de GTA ni binarios en dist. CRLF/diff --check verificados al cierre. Pendiente revisión cruzada y juego por el dueño.
- Commit bloqueado: `git add` no puede crear `.git/index.lock` (Permission denied); `.git` está en solo lectura por el entorno, sin elevación disponible. Sin commit ni push.
- Mensaje sugerido: `feat: billar de a dos por red (SIN REVISAR)`. Cuerpo: `Adaptado de sa-coop (Karimov-Akbar, MIT): src/client/features/story/pool/Pool.cpp; semántica gta-reversed RunningScript.cpp, PadCommands.cpp, RpAnimBlend.cpp, Ped.cpp.`
