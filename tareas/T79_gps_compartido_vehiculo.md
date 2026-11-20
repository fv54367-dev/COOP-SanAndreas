# Tarea T79: GPS del conductor para los pasajeros (T57)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md` y la sección T57 de `tareas\TANDA_2026-11-10_sacoop_karimov.md`. Código: `client/src/CGpsRoute.*`. **Única tarea de esta cadena que puede tocar el protocolo** (`shared/network`): si lo cambiás, versión siguiente a la de `shared/config.h`, paquete al final de `packet_types.h` y `ePacketType_ToString` en el mismo orden.

Hoy cada uno ve solo la ruta a su propio punto del mapa y solo si maneja. Pedido del dueño: **los que van en el mismo vehículo que un jugador conductor ven la ruta al punto del conductor** (mientras el conductor tenga punto marcado). Si el pasajero tiene su propio punto, gana el del conductor mientras vayan juntos (decisión: un solo destino por auto). Al bajarse o cambiar de vehículo vuelve a lo de hoy.

Diseño sugerido: el conductor manda su punto (posición; o "sin punto") al cambiar y cada 1–2 s solo si hay jugadores en su vehículo; el server lo reenvía solo a los ocupantes de ese vehículo que estén en coop (F1). El pasajero calcula la ruta en su juego con el mismo código de hoy (pedido de áreas de nodos propio). Dibujar también aunque el pasajero no maneje (hoy se exige conducir).

## Casos borde
3–4 en el mismo auto (moto con acompañante, bus); conductor NPC o sin jugador: nada; conductor se desconecta o en F1 individual; pasajero en individual: no recibe; interior/cinemática/radar oculto: no dibuja; `gps-route=false` en el pasajero: no dibuja; en el conductor: no manda.

## Aceptación
Compilan client/server/testbot, `tools\correr_testbot.bat` TODO PASS, CRLF, interruptor documentado en `INTERRUPTORES.md`. Prueba simple en `TESTEAR_HOY.md` (T57) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
