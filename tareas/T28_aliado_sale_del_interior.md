# Tarea T28_aliado_sale_del_interior: el seguidor no se queda adentro cuando sale la autoridad
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo salvo que sea imprescindible. Interruptor nuevo `mission-ally-follow-area` (por defecto `true`; documentalo en `INTERRUPTORES.md`).

## Problema (probado en juego)
Cleaning the Hood: al entrar el host, Ryder se teletransporta con él. Al salir no, y después la puerta queda cerrada: el host ya no puede volver a buscarlo.
Evidencia: SWEET1B reactiva Ryder como miembro del grupo y habilita `EnableEntryExitPlayerGroupWarping` en las dos puertas (`SWEET1B.txt:2309-2324`). Al salir, `CEntryExit::WarpGangWithPlayer` (gta-reversed `EntryExit.cpp:566`, 0x43F1F0) debería llevarlo y fijarle el área. El script solo lo mueve si el host está en la puerta y Ryder a más de 30 m (`:2416-2429`), y cierra la puerta cuando Ryder está a menos de 10 m (`:2467-2474`). Ese LOCATE del host además queda retenido por `rule-wait-interior` mientras el invitado sigue adentro (log real: `rule-wait-interior opcode=00FE at=2167.6,-1672.6`).

Causas a verificar en el código, antes de elegir el arreglo:
- Que la copia de red o el traspaso de interior (`CInteriorOwnership`, `interior-handover`) le pisen a Ryder posición o área después del warp, porque el invitado que quedó adentro pasó a ser su dueño.
- Que el ped quede con un `m_nAreaCode` distinto del de la cámara: existe pero no se dibuja.

## Cambio
En la autoridad, durante una misión de historia coop: cuando el jugador local cambia de área (entrar o salir de un interior), durante los 3 s siguientes revisá los miembros de su grupo. Si un miembro vivo sigue en el área anterior o a más de 30 m, llevalo junto al jugador usando lo mismo que `WarpGangWithPlayer` (`Teleport`, limpiar tareas, `SetAreaCode` al área del jugador) y asegurate de que el dueño de red de ese ped no lo devuelva al lugar anterior (reutilizá el mecanismo existente de toma de propiedad; no inventes uno). Log `rule-ally-follow-area`.

Una sola vez por cambio de área y por ped. No toques la lógica de `rule-wait-interior`. Si un jugador invitado sigue adentro, tiene que poder salir igual: comprobá que `SwitchEntryExit False` no le cierre la salida y describilo en el resumen.

## No cambia
Peds que no son del grupo; misión individual; un ped en auto.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md`, en "Pendiente" de `ESTADO.md` y 3–5 líneas de crítica en la tanda. No instales ni toques los GTA. Resumen ≤8 líneas, con cuál de las causas encontraste.
