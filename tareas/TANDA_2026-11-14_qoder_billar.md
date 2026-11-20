# TANDA 2026-11-14 — Qoder: billar cooperativo

Pedido del dueño: al aceptar con F7 el invitado queda mirando al techo y la partida no empieza para ninguno; las bolas aparecen pero una tirada no se ve igual en ambos juegos. Evidencia de la última prueba: `pool-start`, `pool-view-tx/rx` y 18 mirrors llegan en ambos; no hay `pool-view-invalid`.

Objetivo: conservar POOL2 nativo y su única autoridad en el iniciador, corregir cámara/animación del invitado y hacer que las vistas de bolas dejen de competir con el tráfico fiable de SCRIPT.

Archivos: `client/src/CCoopPool.cpp`, `server/src/PacketHandlers/scripts.cpp`, `shared/network/packets/scripts.h`, `shared/config.h`, `tools/testbot/testbot.cpp`, `docs/SISTEMAS.md`, `ESTADO.md`, `TESTEAR_HOY.md`, `INTERRUPTORES.md` y esta tanda. Protocolo 0.4.52 -> 0.4.53 por cambio de canal de VIEW. Sin SCM, sin tocar otras misiones, CRLF en C++/H.

## Casos borde
- 3-4 jugadores: solo el par aceptado juega; los cercanos siguen siendo observadores.
- F1 individual, otra área o más de 8 m: no se acepta ni se reenvía la sesión.
- Desconexión, muerte, F7 o pérdida de vistas: limpieza nativa y restauración de control/cámara.
- Paquetes VIEW viejos o perdidos: se descartan por sesión/secuencia; el siguiente estado completo reemplaza al anterior.
- Interruptor apagado: comportamiento anterior, sin paquetes nuevos.

## Criterio de aceptación
- Compilan cliente, servidor y testbot; escenario F pasa, incluidos canales y pulsaciones consecutivas.
- En juego (lo prueba el dueño): ambos entran a la mesa sin cámara al techo; el invitado puede jugar su turno; una misma tirada muestra las mismas bolas y posiciones finales; F7 sale limpio.
- Queda compilado e instalado, pero SIN PROBAR en juego hasta confirmación del dueño. Resumen <=10 líneas.

## Hecho
- `CoopPool` elige canal por acción: VIEW por SYNC con secuencia; señalización, INPUT y resultado por SCRIPT fiable para no perder botones ni deltas de mouse. Protocolo 0.4.53.
- Cámara de transición con origen/objetivo/FOV coherentes, cámara persistente en el invitado, rumbo de matriz y velocidad residual corregidos; restauración al salir.
- Servidor reenvía cada INPUT sin tirar el último botón al compactar mouse. Hash de VIEW con posición, las tres orientaciones, escala y animación.
- Test F nuevo: canales y dos inputs consecutivos; el anterior descarte del servidor habría fallado el segundo.
- Compilados cliente/server/testbot. Testbot completo: 14 escenarios, 134 chequeos, 0 fallas + escenario D, 3 chequeos, 0 fallas.
- Instalado en GTA_JUGADOR1/2/3 y server; modo de dos jugadores reabierto. SIN PROBAR en juego.

## Revisión y cierre de Claude (14-11)
- Kimi prendía CAMERA_PERSIST_* sin escribir `m_vecMoveLinear`/`m_vecTrackLinear`: la cámara del invitado tomaba vectores viejos. Ahora, como sa-coop `Pool.cpp`: cámara fija una sola vez y cada frame posición/mira/FOV suavizados (corte si salta >3 m).
- Portado además de sa-coop: bolas en el área visible (`CGame::currArea`) y con modelo creado si falta; se oculta también el rival del bar propio (1@ del script `pool`); invitado sin radar/HUD y con el arma oculta; INPUT por canal de estado cada frame (estado completo, se reemplaza el perdido).
- Compilados cliente/server/testbot, testbot TODO PASS. Instalado. SIN PROBAR en juego.
