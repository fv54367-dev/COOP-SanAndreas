# Tarea T95: Wrong Side of the Tracks (SMOKE3, id 29) en coop

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `docs\MISIONES_MODO.md` y `docs\SISTEMAS.md` (qué sistemas de coop existen: espejo de misión, `mission-guest-counts` T63, acciones de invitados T66, reglas de misión `CMissionSyncRules`, autos de invitado T40, espera de todos en marcadores, compañeros en cinemáticas T58/T85/T86).

Script original: `_refs\project_gta_sa_coop\CoopAndreas\scm\scripts\SMOKE3.txt` (solo lectura; **no toques `main.scm` ni `scm/`**).

Referencia permitida: `_refs\sa-coop-karimov` (MIT). Se puede portar código citándolo en `NOTICE.md` y en el commit; ver `src\client\features\story\missions\Missions.cpp`, `mission-rules`, `cutscene-partner` y `cutscene-clips`. **SACoop (`_refs\sacoop`) no tiene licencia: nunca copiar de ahí.**

## La misión
Smoke en moto detrás del tren, CJ dispara a los Vagos del tren; si se aleja demasiado, falla.

## Qué hacer
1. Recorré el script de punta a punta pensando en 2–4 jugadores, con el dueño de la misión y los invitados en coop. Escribí en la tanda (`tareas\TANDA_2026-11-10_codex_T95.md`) cada punto donde un invitado:
   - queda trabado o afuera (no puede seguir, no tiene vehículo, no puede disparar o no cuenta);
   - rompe la misión (el script falla porque mira solo al dueño);
   - ve algo raro: cinemática, textos o marcadores que no le llegan.
2. Arreglá cada punto con los sistemas que ya existen antes de inventar uno nuevo. Ejemplos: lo que hace el invitado cuenta para la misión, un vehículo propio para cada invitado (separado unos 2 m, a la vista, sin quedar metido en estructuras), los invitados ubicados o fuera de plano en las cinemáticas, y la espera de todos solo donde tenga sentido.
3. **Cinemáticas de la misión (pedido del dueño):** que los invitados se vean bien. Si la escena lo necesita, con animaciones que tengan sentido: caminar con el grupo, escuchar, mirar al que habla, gestos, sentarse si los demás se sientan. Usá `CSceneMocap`/`CScenePartner`, portados de Karimov, y las reglas por escena de T86. Donde no haya un lugar coherente, fuera de plano. Anotá en la tanda qué hiciste en cada cinemática.
4. Detrás de un interruptor (uno por misión o uno general `coop-missions-ls2`), documentado en `INTERRUPTORES.md`. Apagado: comportamiento de hoy.
5. Usá gta-reversed para cualquier tarea u opcode nativo que llames, y citá la función.

## Casos borde
- 3–4 jugadores.
- Invitado en F1 individual.
- Muerte o arresto de cualquiera: la misión falla para todos.
- Abandono: se va solo el que abandona.
- Desconexión a mitad.
- Invitado lejos o en otro interior.

## Aceptación
Compilan client/server/testbot, `tools\correr_testbot.bat` TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (sección Wrong Side of the Tracks), fila en `docs\MISIONES_MODO.md` y "Pendiente" de `ESTADO.md`. Si cambiás el protocolo: versión siguiente a la de `shared/config.h` y paquete al final. No instales. Resumen ≤10 líneas.
