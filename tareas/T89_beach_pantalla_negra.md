# Tarea T89: Life's a Beach, al invitado se le pone la pantalla negra al bailar (T54)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\T54_life_beach_baile_coop.md` y su salida. Código: lo que haya hecho T54 (grep `coop-dance`, `DANCE`), `CCameraMirror.*`, fades (`CCamera::Fade`, `DO_FADE` 016A), `CMissionMirror`.

## Qué reportó el dueño
El host puso para bailar ("Obtén una puntuación de 2500 o más para acceder a la furgoneta con el equipo de música"). En el invitado la pantalla quedó negra del todo, aunque podía seguir moviéndose y haciendo cosas. Volvió a la normalidad recién al cargar partida o morir.

## Qué hacer
1. Encontrá qué deja la pantalla negra en el invitado. Candidatos:
   - un fade espejado del host que nunca se revierte (el `DO_FADE` de entrada al baile sin el de salida);
   - la cámara o el widget del baile arrancado en el invitado sin el minijuego;
   - la pantalla del minijuego de ritmo de T54 sin su parte visual.
2. Arreglo de raíz. Además, como red de seguridad: si el invitado queda con fade a negro y no hay cinemática, minijuego propio ni carga en curso, se restaura a los pocos segundos y queda un log `fade-stuck-restore`.
3. Dejá que el invitado baile (T54) o mire, pero nunca a ciegas.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (sección T54) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
