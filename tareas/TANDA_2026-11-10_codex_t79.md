# T79 GPS compartido del conductor
Objetivo: un destino por auto; pasajeros coop usan el punto del conductor mientras este lo comparte.
Archivos: client/src/CGpsRoute.cpp, client/src/CGpsRoute.h, client/src/PacketHandlers/Players.cpp, server/src/PacketHandlers/Players.cpp, shared/network/packets/gps.h, shared/network/packet_types.h, shared/config.h, INTERRUPTORES.md y esta tanda.
TESTEAR_HOY.md y ESTADO.md reservados por T78 (#48): texto de integracion al final.
Restricciones: CRLF, sin SCM, hooks nuevos ni instalacion; protocolo 0.4.39-alpha; paquete al final y nombres en orden.
Aceptacion: client/server/testbot compilados, regresion TODO PASS; revision cruzada y juego pendientes.
Critica previa del protocolo: la propuesta no cubre paquetes atrasados tras cambio de asiento/auto ni punto viejo al apagar GPS. Validar identidad de conductor y vehiculo en ambos extremos y caducar el destino; enviar sin punto cuando lo borra.
Casos borde:
- 3-4 ocupantes: envio dirigido a todos los pasajeros coop del mismo vehiculo, no espectadores.
- F1 individual: no comparte ni usa destino remoto; conductor NPC no comparte.
- Muerte/desconexion/cambio de auto o conductor: validacion local y caducidad 2500 ms, sin punteros persistentes.
- Interior/cinematica/radar oculto: mismas guardas de T57; pasajero calcula nodos propios.
- gps-route=false no envia ni dibuja; sin destino compartido conserva comportamiento T57 (solo conductor).
- Punto propio de pasajero intacto, nunca modificar blips ni el menu.
Referencia: gta-reversed PathFind.h:332,398-407; se reutiliza el buscador T57 sin nuevos offsets.

## Implementacion (SIN REVISAR / SIN PROBAR / SIN INSTALAR)
- VEHICLE_GPS al final del protocolo 0.4.39-alpha, SYNC; coordenadas finitas acotadas y conductor sellado por server.
- Conductor coop con GPS envia cambios (max 10/s) y latido 1 s solo si hay pasajero jugador coop. Server valida conductor/asientos/vehiculo/area/vida y posiciones recientes; solo reenvia a pasajeros coop.
- Pasajero revalida conductor real, modo, vida y vehiculo cada frame; destino caduca 2500 ms. No cambia blips ni punto propio; nodos y ruta propios como T57.
- Referencia gta-reversed PathFind.h:332,398-407 (busqueda y pedido unico de nodos de T57). Sin hooks/offsets ni fuentes externas nuevos.
- El lanzador original escribe fuera del workspace permitido: copia tools/correr_testbot_t79_local.bat con carpetas dentro del repo, puertos 6819/6820 y cierre limitado a esas carpetas.
- Archivos adicionales reservados #52: tools/testbot/testbot.cpp y tools/correr_testbot_t79_local.bat (escenario k de cuatro bots).

## Documentacion compartida pendiente por reserva #48 (T78)
TESTEAR_HOY.md, T57:
T79: A conduce y B/C/D pasajeros coop con gps-route=true. A marca/cambia/borra punto: todos ven ruta al destino de A; punto propio de B no cambia. Bajarse/cambiar auto y volver a conducir recupera su ruta propia. Repetir moto y bus. F1 individual en conductor o pasajero, conductor NPC, muerte/desconexion, interior/cinematica/HUD oculto y GPS apagado: no ruta compartida (apagado en conductor caduca hasta 2,5 s). No afectar espectadores de otro auto.

ESTADO.md, Pendiente (revision y prueba):
T79 GPS compartido, SIN REVISAR / SIN PROBAR / SIN INSTALAR: gps-route comparte punto de conductor coop solo con pasajeros coop del mismo auto. Protocolo 0.4.39-alpha VEHICLE_GPS; server valida asientos y sella conductor, pasajero calcula ruta/nodos propios y caduca 2,5 s; conserva punto propio. Referencia gta-reversed PathFind.h:332,398-407. Revision cruzada y prueba visual pendientes; ver tanda T79.

## Validacion final
- xmake build client OK (23,938 s inicial; 2,125 s final); server OK (7,250 s); testbot OK (3,734 s final).
- Lanzador T79: 10 escenarios / 98 chequeos + escenario d / 3 chequeos, cero fallas: TODO PASS.
- Tras corregir la numeracion de asientos del fixture a pasajeros 0/1/2, escenario k repetido: 8 chequeos TODO PASS.
- git diff --check y CRLF de todos los C++/headers tocados OK. Enum y ToString en el mismo orden.
- Sin instalacion, SCM, juego ni commit (.git solo lectura); reservas #51/#52 mantenidas hasta commit del orquestador.
- ESTADO.md y TESTEAR_HOY.md no editados por reserva ajena #48: integrar los textos preparados arriba. INTERRUPTORES.md actualizado.
