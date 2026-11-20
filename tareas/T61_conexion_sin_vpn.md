# Tarea T61: conectarse sin VPN (UPnP, NAT punching, código de conexión)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`.

## Pedido
Hoy se juega por Radmin VPN. Que el que hostea pueda abrir el puerto solo y que los demás entren con un código, sin VPN.

## Referencia (MIT, portar)
`_refs\sa-coop-karimov\src\common\api\net\` y su server (`src\server`): UPnP para abrir el puerto UDP, NAT punching, relay TURN opcional, y un **código de conexión** que lleva la IP externa del host y el puerto. Buscá dónde lo arman, cómo detectan la IP externa (sin servicios de terceros raros) y qué hacen si falla.

## Cambio (fase 1)
- Server: interruptor `upnp` en `server-config.ini` (default **true**): al arrancar intenta abrir su puerto UDP por UPnP y lo cierra al salir; imprime el resultado y el **código de conexión**.
- Cliente: en la pantalla/ini de conexión acepta ese código además de IP:puerto.
- NAT punching y TURN: **solo si entran limpios y sin servidor externo nuestro**; si no, dejalo escrito en la tanda como fase 2.
- Nada de esto cambia Radmin: con `upnp=false` todo como hoy.

## Casos borde
- Router sin UPnP o puerto ocupado: aviso claro en la consola del server, sigue funcionando por VPN/IP.
- Dos servers en la misma PC (modo prueba, puertos 6799/6800/el normal): cada uno su puerto, sin pisarse; el modo prueba local no intenta UPnP.
- Server que crashea sin cerrar el puerto: la regla UPnP tiene tiempo de vida (lease) para no quedar abierta.

## Aceptación
Compilan client/server/testbot. CRLF. Cita MIT y la licencia de cualquier librería nueva en `NOTICE.md` (preferí no agregar dependencias: si hace falta una, que sea chica y MIT/BSD, en `third_party/`). `TESTEAR_HOY.md`, `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.
