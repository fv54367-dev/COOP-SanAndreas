# Reclutas Grove y puertas de garaje
Completar los dos pendientes del MD con scripts originales. Reservados: client/src/CGarageSync.cpp, client/src/CNetworkPedManager.cpp, client/src/PacketHandlers/peds.cpp, server/src/CHandover.cpp, server/src/CNetworkPed.h, server/src/PacketHandlers/peds.cpp, server/src/PacketHandlers/world.cpp, shared/config.h, shared/network/packet_types.h, shared/network/packets/peds.h, shared/network/packets/world.h, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md tools/testbot/testbot.cpp y esta tanda. Protocolo 0.4.45-alpha, mantener CRLF.
- Con 3–4 jugadores solo el primer reclutador validado controla cada NPC y cada receptor recibe las puertas.
- Individual conserva su lógica de misión. En individual las puertas remotas solo aplican el movimiento visual.
- Muerte, desconexión, cambio de interior y distancia liberan o impiden reclamos según el grupo nativo y autoridad de servidor.
- Los reclutas son ambientales; excluir peds de misión. Las puertas de servicios remotas no ejecutan transacciones.
- Interruptores existentes restauran comportamiento previo.
Aceptar al compilar cliente, servidor y testbot y documentar pruebas de juego. Resumen final de hasta diez líneas.

Verificado: client, server y testbot compilan. ENet local: 15 escenarios y 133 chequeos PASS, incluidos garajes en F1, reconexión y autoridad de reclutas. Pendiente de revisión cruzada y prueba con GTA.
