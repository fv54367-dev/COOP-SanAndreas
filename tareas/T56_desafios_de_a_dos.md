# Tarea T56: desafíos de a dos (básquet y demás actividades)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md`. No toques `main.scm` ni `scm/`.

## Pedido del dueño
Las actividades sueltas (básquet y las demás que tengan un puntaje) se pueden jugar como desafío entre dos jugadores.

## Referencia (MIT)
`_refs\sa-coop-karimov\src\client\features\story\contest\`: quien arranca una actividad puede desafiar a otro (U) en sus primeros 20 s; quien acepta (Y) juega lo mismo en su propio juego y al terminar cada juego dice quién ganó. Leé la lista de actividades que soportan (básquet, etc.), de dónde leen el puntaje de cada una y cómo arrancan la misma actividad en el otro juego.

## Reglas nuestras que se mantienen
- Cada uno juega **en su propio juego** y cobra su propio premio (dinero/estadísticas de cada uno). Solo se comparan los puntajes.
- **No** entran el baile ni el lowrider (VS descartado por el dueño; esos van en equipo, T53/T54) ni el billar (T55).
- Reusá lo que haya del VS de carreras (`CVsRace`) y `CMinigameVs` para invitación, HUD y tabla, sin reactivar el VS de baile/lowrider.

## Cambio
Interruptor `activity-challenge` (cliente, default **true**, `INTERRUPTORES.md`).
- Al arrancar una actividad soportada, aviso en pantalla "U: desafiar". El desafiado ve "<nombre> te desafía a <actividad>: Y/N" y, si acepta y está cerca, se le arranca la misma actividad.
- Al terminar los dos: mensaje en chat y en pantalla con los puntajes y el ganador.

## Casos borde
- 3–4 jugadores: el desafío es entre dos; si querés que acepten varios, que gane el mayor puntaje (decilo).
- El desafiado está lejos, en misión o en individual: no recibe el desafío.
- Uno se va o abandona: el otro gana por abandono, sin colgar nada.
- Interruptor apagado: como hoy.

## Aceptación
Compilan client/server/testbot (versión subida). CRLF. Cita MIT. `TESTEAR_HOY.md` (una prueba por actividad), `INTERRUPTORES.md`, tanda. No instales. Resumen ≤10 líneas.
