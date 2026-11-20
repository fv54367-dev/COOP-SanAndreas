# Tarea T23_marcador_espera_host: el marcador del invitado que maneja el auto de misión también espera al host
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-08d.md`. Corrige T8/T12 (`FilterGuestCar` en `client/src/CMissionSyncRules.cpp`). Sin cambios de protocolo.

## Problema (probado en juego)
Drive-By (SWEET4): si el invitado maneja el auto de misión y llega primero a un marcador rojo (`SWEET4.txt:1469-1472`, `:3041-3044`), la escena arranca sin el host. `FilterGuestCar` da por cumplido el LOCATE con la posición del auto y `IS_CHAR_SITTING_IN_CAR`, y la espera existente solo mira a los jugadores de red, nunca al propio host. El objetivo del proyecto es que los marcadores rojos esperen a todos.

## Cambio
Cuando un LOCATE sobre el ped local es verdadero **solo por el override del auto del invitado** (`bGuestCarOverride`) y es un marcador chico (punto fijo o `locate.radius <= 20.0f`, como la regla de espera existente), exigí además que el host esté cerca del mismo punto, con la misma tolerancia que se usa para los demás jugadores en la espera (~12 m o 3 veces el radio; reutilizá esa lógica), a pie o en cualquier vehículo y en la misma área. Si no está: devolvé falso y mostrá en el chat del host, cada 5 s, "Acercate al marcador para seguir la mision". Log `rule-wait-host-guest-car opcode= at=`.

Sigue sin hacer falta que el host esté **dentro** del auto: eso es lo que resolvió Ryder (auto de 2 asientos).

No exijas al host en:
- LOCATE grandes (`radius > 20`), porque son controles de distancia, como los de 85/150 en SWEET4.
- `IS_CHAR_IN_CAR` y `IS_CHAR_SITTING_IN_CAR`, que siguen como T12: el objetivo "subí al auto" avanza. La espera se aplica en el LOCATE del marcador. Pero si un mismo `if and` combina el LOCATE con `IS_CHAR_SITTING_IN_CAR`, el LOCATE falso alcanza.
- Marcadores de garaje o amarillos que no sean LOCATE de misión: no cambian.

## Casos borde
Host muerto o arrestado: no se lo espera, para no trabar la misión (la misión falla igual por su lógica). Host en otro interior: cuenta como lejos. Interruptor `mission-any-player-in-car=false`: comportamiento actual. Las misiones de `requiresHostCar` no cambian. Crítica previa: 3–5 líneas en `tareas/TANDA_2026-11-08d.md`.

## Aceptación
Compilan client y server. Diff acotado + una línea simple en `TESTEAR_HOY.md`. No instales ni toques los GTA. Resumen ≤6 líneas.
