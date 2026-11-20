# Tarea T59: chat de voz por cercanía
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`.

## Pedido
Hablar por micrófono con los jugadores cercanos: el volumen baja con la distancia.

## Referencia (MIT, portar)
`_refs\sa-coop-karimov\src\client\features\voice\`: captura y reproducción con la API de Windows, códec IMA ADPCM (4 bits por muestra de 16 bits, cada trama lleva su estado inicial para que un paquete perdido no desincronice: `[i16 predictor][u8 step][u8 0][n/2 bytes]`), envío por un canal no confiable del server, volumen por distancia. Buscá también su tecla de hablar y su opción de abrir el micrófono.

## Cambio
- Interruptor `voice-chat` (cliente, default **true**) y `voice-chat` en `server-config.ini` (default true; el server solo reenvía a los que están a menos de N metros, N configurable `voice-range`, default 50). En `INTERRUPTORES.md`.
- Tecla para hablar (pulsar y mantener; elegí una libre y mostrala en F1). Opción en F1 para silenciar a todos.
- Ícono o texto chico en pantalla mientras alguien habla (nombre).

## Casos borde
- Sin micrófono o sin permiso: no falla, avisa una vez en el chat.
- 3–4 hablando a la vez: se mezclan.
- En interior distinto o en otra punta del mapa: no se oye.
- Interruptor apagado (cliente o server): no se captura ni se envía nada.
- El juego en pausa/alt-tab: el micrófono no queda abierto si soltás la tecla.

## Aceptación
Compilan client/server/testbot (versión subida). CRLF. Cita MIT. `TESTEAR_HOY.md`, `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.
