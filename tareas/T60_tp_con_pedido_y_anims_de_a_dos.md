# Tarea T60: `/tp` con pedido Y/N y animaciones de a dos
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`, `docs/REQUISITOS.md` (sección 4, comandos de modo prueba).

## Parte A: teletransporte con permiso
Referencia (MIT): `_refs\sa-coop-karimov\src\client\features\teleport\`. Hoy `/tp`, `/ir`, `/traer` (modo prueba) teletransportan sin preguntar.
- Interruptor `tp-request` (server, default **true**): fuera del modo prueba, `/tp <jugador>` (ir hacia él) y `/traer <jugador>` piden permiso: al otro le sale "<nombre> quiere ir hacia vos / traerte: Y/N" (15 s). En modo prueba de 2 instancias, como hoy (sin preguntar).
- No se puede durante una misión coop en curso de alguno de los dos (mensaje claro). Dentro de un vehículo: lo saca o lo pone de acompañante, como haga hoy el comando.

## Parte B: animaciones de a dos
Referencia (MIT): `_refs\sa-coop-karimov\src\client\features\anims\` (`Emotes.cpp`, `Anims.cpp`, `AnimPreview.cpp`): ventana de animaciones (U o `/anims`) con las del juego, 16 de a dos (saludo, abrazo, etc.: uno invita, el otro acepta, se colocan enfrentados) y `.ifp` propios del jugador en `coop\anims`. **No copies las animaciones de terceros** (los paquetes de LibertyCity/MTA tienen otros autores): solo el código y las del juego.
- Interruptor `emotes` (cliente, default **true**). Revisá que la tecla no choque con las nuestras (F1, F6/T, F8, U del desafío de T56; si choca, elegí otra y avisá).
- Se ve igual en todos los juegos (reusá el sync de animaciones con nombre que ya hay: `npc-named-anim-sync` / animaciones de jugador).

## Casos borde
- 3–4 jugadores: el pedido va a uno solo; la animación de a dos, entre dos.
- El otro está lejos, en misión, en vehículo o muerto: no se ofrece.
- Interruptores apagados: como hoy.

## Aceptación
Compilan client/server/testbot (versión subida si hace falta). CRLF. Cita MIT. `TESTEAR_HOY.md`, `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.

## Nota (2026-11-10 08:30)
Una sesión anterior se cortó por límite de uso: **ya hay trabajo de esta tarea sin commitear en el árbol**. Mirá `git status` y `git diff`, seguí desde ahí y no rehagas lo que ya está bien.
