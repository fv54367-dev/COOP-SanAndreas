# T60 ? pedidos de teletransporte y emotes
Objetivo: ejecutar T60; Codex escribe, queda SIN REVISAR hasta revisi?n cruzada.
Archivos: client/src/CSocialActions.{cpp,h}, CEmoteIfp.h, CChatCommands.cpp, Main.cpp, CMinigameVs.cpp, CConfigLoader.{cpp,h}, PacketHandlers/scripts.cpp; server/src/PacketHandlers/scripts.cpp, ConfigManager.{cpp,h}; shared/network/packets/scripts.h, packet_types.h, shared/config.h; tools/testbot/testbot.cpp; NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/SISTEMAS.md, esta tanda y tanda saco??_karimov.
Protocolo: due?o Codex T60, siguiente versi?n 0.4.29-alpha. Reserva #23.
Restricciones: CRLF en C++, sin SCM, sin instalar ni jugar; no animaciones de terceros. Resumen <=10 l?neas.
Cr?tica previa: falta separar pedidos simult?neos, respuesta vieja, ingreso a misi?n entre oferta y aceptaci?n, streaming y archivos IFP distintos. Servidor vincula sesi?n/dos identidades/tipo, caduca en 15 s, revalida al aceptar. No confiar coordenadas del solicitante.
Casos borde:
- 3?4 jugadores: destino expl?cito por nombre; una sesi?n por participante, espectadores solo reciben poses aceptadas.
- Individual, muerto, veh?culo, distinta ?rea o misi?n: emotes no ofrecidos; perder condiciones cancela. TP rechaza misi?n de cualquiera, permite distancia en la misma ?rea.
- Desconexi?n, caducidad, respuesta repetida: sin ejecuci?n. No reproducir un pedido al reconectar.
- U es de T56: usar F9; ventana manejada con flechas/Enter para no apropiarse del mouse/pad.
- Interruptores apagados: sin nuevas acciones; modo prueba conserva comandos anteriores sin consentimiento.
- IFP propios: lectura limitada/validada, nombres por contenido, solo fuera de cinem?ticas; faltantes remotos omiten pose.
Aceptaci?n: client/server/testbot compilan; validaci?n de red; documentaci?n y MIT. No instalar.
