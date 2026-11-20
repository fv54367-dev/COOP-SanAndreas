# TANDA 2026-11-13 (Claude, rama `peticiones-lyrax`, worktree `_lyrax`)

Origen: pedidos de Lyrax. Todo queda **SIN PROBAR** hasta que el dueño confirme. Se integra a `main` al final.

## Puntos
1. **Marcador del auto del invitado** (`guest-car-blip-hide`): el ícono/flecha de un vehículo de misión desaparece para quien está adentro, y vuelve si se baja.
   - Receptor (`CMirrorBlips.cpp`, `Reconcile`): un blip espejado de vehículo no se crea mientras el jugador local está en ese vehículo.
   - Autoridad (`CCommandPutNetworkPlayersInCar.cpp`, `ProcessParkedCopies`): el blip de la copia estacionada pasa a "ni radar ni marcador" mientras la ocupa otro jugador; al bajarse vuelve (la capa espejo lo replica a todos).
2. **Comandos `/tpm` y `/fijar`** (`chat-tpm-fijar`), en `CChatCommands.cpp` + `CVehicleSurf.*` nuevo:
   - `/tpm`: teletransporte al marcador del mapa (a pie o con el vehículo que manejás), con búsqueda de piso; sale del interior.
   - `/fijar`: parado arriba de un vehículo, quedás pegado a él (posición y giro relativos, misma velocidad); otra vez `/fijar`, saltar, subir a un vehículo, morir o que el vehículo desaparezca lo sueltan.
3. **TAB nuevo** (`CNetworkPlayerList.*`, `CPlayerMoney.*` nuevo, paquete `PLAYER_MONEY`, protocolo 0.4.49): panel arriba a la izquierda con logo COOP SA, "Jugadores n/4", línea amarilla; por fila: barra amarilla y "Tú" en el propio, nombre, dinero verde (pricedown), vida (roja) y blindaje (blanca), ícono del arma y ping "NN ms" verde (<= 80), amarillo (<= 150) o rojo.

## Archivos
`client/src/CMirrorBlips.cpp`, `client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp`, `client/src/CChatCommands.cpp`, `client/src/CVehicleSurf.{h,cpp}` (nuevo), `client/src/UI/CNetworkPlayerList.{h,cpp}`, `client/src/UI/CoopSaLogo.h` (nuevo, generado), `client/src/CPlayerMoney.{h,cpp}` (nuevo), `client/src/CNetworkPlayer.h`, `client/src/CConfigLoader.{h,cpp}`, `client/src/Main.cpp`, `shared/config.h`, `shared/network/packet_types.h`, `shared/network/packets/players.h`, `server/src/PacketHandlers/players.cpp`, docs.

## Casos borde
- 3-4 jugadores: el TAB lista todos (propio primero); el marcador se oculta solo para quien está adentro (y en la autoridad mientras lo ocupe cualquiera).
- F1 / otra área: un jugador sin ped local muestra nombre, dinero y ping, con barras vacías y sin arma.
- Desconexión: su dinero se borra con su `CNetworkPlayer`; el que entra recibe el dinero de los demás en <= 5 s (reenvío periódico).
- `/tpm` sin marcador, en cinemática o muerto: mensaje y nada más. `/fijar` sin vehículo debajo: mensaje.
- `/fijar` sobre un vehículo remoto: se pega a la copia local (la posición propia se sigue mandando como a pie).
- Interruptores apagados: `player-list-coopsa=false` vuelve a la lista vieja centrada y no manda el dinero; marcadores y comandos como antes.

## Resultado
- Los 3 puntos compilan (cliente, server, testbot); testbot TODO PASS (worktree, puertos 6811/6812). SIN PROBAR en juego.
- La "ú" del "Tú" es el código 0xAA de las fuentes del juego (verificado en `fonts.txd`, font1 y font2, con satk).
- Logo: `client/src/UI/CoopSaLogo.h` (luminancia 128x128 de `launcher/coopsa-logo.bmp`) -> textura RW con disco negro.
- Cantidad máxima del "n/4": fija en 4 (el cliente no conoce el `maxplayers` del server); con más jugadores muestra n/n.
