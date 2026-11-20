# Tarea T90: garajes sincronizados (abrir y cerrar)

Leé `AGENTS.md` y `tareas\COMUN_reglas.md`. Código: `client/src/Hooks/WorldHooks.cpp`, `CMissionMode.cpp` y lo que ya toque garajes (grep `Garage`); gta-reversed `Garages.cpp`, `Garage.cpp` (`CGarage::Update`, `OpenThisGarage`, `CloseThisGarage`, `m_nDoorState`, `m_fDoorPosition`) y los opcodes de garaje (`OPEN_GARAGE` 0360, `CLOSE_GARAGE` 0361, `SET_GARAGE_TYPE`, etc.).

## Qué pidió el dueño
Si cerca de un jugador un garaje se abre o se cierra, los demás jugadores tienen que verlo igual. Sirve sobre todo para misiones (garajes que abre el script, talleres, Pay 'n' Spray, garajes de la casa).

## Qué hacer
1. Un solo dueño por garaje: el que lo abrió o cerró, o la autoridad de la misión si fue el script. Cuando el estado de la puerta cambia (cerrado, abriendo, abierto, cerrando), se lo manda a los demás y ellos lo aplican con la API del juego, sin teletransportar la puerta.
2. El que se acerca después recibe el estado actual.
3. Pensá qué pasa con los garajes que reaccionan al jugador local (Pay 'n' Spray, talleres de tuneo, garajes de guardar autos): no romper su lógica en cada juego; si hace falta, sincronizá solo los de script o los abiertos/cerrados por misión, y documentalo.
4. Interruptor nuevo (default true) en `INTERRUPTORES.md`. Si necesitás paquete nuevo: versión siguiente a la de `shared/config.h`, paquete al final de `packet_types.h` y `ePacketType_ToString` en el mismo orden.

## Casos borde
- Dos jugadores abren a la vez.
- Garaje en otro interior o área.
- Desconexión del dueño.
- 3–4 jugadores.
- Interruptor apagado: comportamiento de hoy.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
