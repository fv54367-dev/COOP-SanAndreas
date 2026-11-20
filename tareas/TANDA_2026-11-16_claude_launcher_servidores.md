# Tanda 2026-11-16 (Claude): launcher tanda 4, servidores en tu red

Objetivo: una pantalla aparte en CoopSA (cuadradito arriba a la izquierda) con los servers de la red local: nombre, jugadores, ping y versión; tocar uno pone su IP/puerto y hace lo de JUGAR. Decisión del dueño (16-11): por ahora solo red local, sin lista pública.

Archivos: `launcher/src/server.rs` (`discover`), `launcher/src/main.rs` (pantalla SERVIDORES). Server sin cambios: ya contesta `COOPSA?1` en su puerto (`CNetwork.cpp` `LauncherQuery`), también a un broadcast.

Casos borde:
- La PC del host contesta por 127.0.0.1 y por cada IP propia: sale una sola vez, como 127.0.0.1.
- Servers de prueba en 6768-6770 y el puerto de `SERVIDOR\server-config.ini`: también se preguntan.
- VPN tipo Radmin/ZeroTier/Hamachi: se manda al broadcast de cada adaptador (/24, /16, /8 porque la máscara no se sabe).
- Firewall de Windows que bloquea server.exe: no aparece; el texto lo dice y queda escribir la IP a mano.
- Server de otra versión: aparece en amarillo; al tocarlo JUGAR avisa como siempre.
- Server lleno: lo dice JUGAR al tocarlo.

Hecho (Claude, sin probar desde la ventana):
- `server::discover(800)`: dos rondas de `COOPSA?1` a broadcast + IP propia, sin repetidos, ordenado por ping.
- Pantalla SERVIDORES: BUSCAR DE NUEVO, hasta 8 filas, se vuelve a buscar sola cada 3 s, VOLVER; tocar una fila llena los campos y corre JUGAR (chequeos y PARTIDAS).
- Probado: `cargo test -- --ignored discover` con `server\server.exe` abierto: lo lista una vez (127.0.0.1:6767, 1 ms). Falta probar con otra PC en la red.
