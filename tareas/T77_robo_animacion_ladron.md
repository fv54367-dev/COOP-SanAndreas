# Tarea T77: robo entre jugadores, que se vea al ladrón (T51/T72)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md` y la sección T72 de `tareas\TANDA_2026-11-10_sync_visual.md`. Código: `client/src/CJackSync.*` y lo que maneje la copia del jugador remoto al entrar a un vehículo (grep `player-jacked`, `ENTER_VEHICLE`, `TaskComplexEnterCar`). **No toques `CreateJackedVictimTask`/`StartNativeDraggedOut`** (arreglo de crash de motos de Claude, commit b9a1769) salvo para llamarlas.

## Qué reportó el dueño (2026-11-10)
1. El que roba ve todo fluido. La víctima ve que su propio CJ hace la animación de que lo sacan, pero **la copia del ladrón no hace la animación de abrir y sacarlo**: se queda quieta.
2. Jugador A va de acompañante delantero; B aprieta F junto a esa puerta: en la pantalla de B no se ve que esté sacando a A, y A ve que lo sacan "porque sí".
Logs: `E:\juegos\gtasacoopnuevo\_logs_2026-11-10\*mirror.log` (`player-jacked`, `ped-jacked`).

## Qué hacer
En la copia del ladrón (en la PC de la víctima y de terceros), reproducir la secuencia nativa del ladrón: ir a la puerta, abrir y `CTaskSimpleCarSlowDragPedOut` (gta-reversed `TaskComplexEnterCar::CreateDragPedOutSubTask`, TaskComplexEnterCar.cpp:801; `TaskSimpleCarSlowDragPedOut` StartAnim 0x64C010) o, en motos, la animación nativa equivalente. Preferí darle a la copia la tarea del juego (`CTaskComplexEnterCar` como conductor/pasajero contra la víctima) antes que poner una animación suelta. Para el caso 2: que el ladrón local también reproduzca la animación de sacar al acompañante (si el juego no la hace porque la víctima es una copia remota, averiguá por qué en gta-reversed: `ComputeSlowJackedPed`, `IsCarSlowJackRequired`). Si el caso 2 no sale limpio, dejalo anotado (el dueño dijo que no es grave).

## Casos borde
3–4 jugadores mirando; ladrón o víctima se desconecta/muere a mitad; cancelar antes de abrir la puerta; moto/BMX; misión coop en curso; `player-jack-native=false` = comportamiento anterior.

## Aceptación
Compilan client/server/testbot, CRLF, citas gta-reversed. Prueba simple en `TESTEAR_HOY.md` y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
