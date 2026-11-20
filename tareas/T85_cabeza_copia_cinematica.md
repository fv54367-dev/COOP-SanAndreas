# Tarea T85: en las cinemáticas, la cabeza de la copia del otro jugador sube y baja

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-10_pruebas_b.md`.

Código:
- escena: `client/src/CScenePartner.cpp`, `CSceneMocap.cpp`, `CCutscenePartners.*`, `SceneCompat/*`
- lo que mueve la cabeza de los peds de red: `CAimSync`, look-at, `CPedIK`, `m_pedIK`, `LookAt`, `HeadLook`

## Qué reportó el dueño
En la cinemática de Big Smoke y en la de Ryder, la cabeza del otro jugador (solo la cabeza) baja y sube muchas veces seguidas, como un tic. Parece general a todas las cinemáticas.

## Qué hacer
Encontrá quién toca la cabeza de la copia durante la cinemática. Lo más probable son dos sistemas que se pelean, por ejemplo:
- la mirada sincronizada de `CAimSync`/IK contra la animación de la escena;
- el `LookAt` de la escena contra el del jugador remoto;
- la pose del mocap contra la de red.

Mientras corre la cinemática, la cabeza de la copia la maneja solo la escena: apagá lo que sobre y restauralo al terminar.

Citá gta-reversed: `PedIK.cpp`, `Ped::Look*`, `TaskSimpleTriggerLookAt`.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
