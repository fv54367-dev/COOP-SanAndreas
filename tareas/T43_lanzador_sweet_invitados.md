# Tarea T43_lanzador_sweet_invitados: los invitados tienen el lanzador de Sweet
Lee `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-09_claude.md`, `docs/MISIONES_MODO.md` y `tareas\T31_individual_invitados_cesar.md`. Sin cambios de protocolo. No toques `main.scm`.

## Causa (demostrada)
El lanzador `SWEET` lo arranca `INTRO2` al terminar (`start_new_script @SWEET`, `INTRO2.txt:2606`), y esa mision la corre solo el host. Las partidas originales de `GTA_JUGADOR2` no tienen el hilo y su log no trae ningun `solo-launch`, mientras que el host los loguea cada 5 s. Sin ese hilo el invitado no puede iniciar nada de Sweet, ni en F1 ni Cesar.

## Cambio
Clase o funcion nueva, procesada desde el loop de `CMissionSyncRules`, detras de un interruptor nuevo `guest-story-launchers` (default true, documentado en `INTERRUPTORES.md`). Primera etapa: **solo Sweet**, a proposito, para probar el mecanismo antes de extenderlo a Ryder, Smoke y OG Loc.

Con `ms_bVanillaScripts`, conectado, siendo invitado (no autoridad), jugador vivo, juego cargado, sin cinematica y `$onmission == 0`:
1. Toma el progreso de Sweet del host: `m_nSweetProgress` del `CNetworkPlayer` con `m_bIsHost` (0 a 8; -1 o desconocido = no hacer nada; >= 9 = no arrancar, el script se termina solo).
2. Si el contador propio (`ScriptSpace[1808]`) es menor que el del host, igualalo. Nunca lo bajes: el invitado puede ir adelantado si jugo solo. Log `guest-launcher counter from=%d to=%d`.
3. Si no existe un script activo llamado `sweet` (recorre `CTheScripts::pActiveScripts`, comparacion sin mayusculas), arrancalo con `CTheScripts::StartNewScript` en la direccion de la etiqueta. La direccion se busca en memoria, no se escribe a mano: escanea el bloque principal de `ScriptSpace` por el opcode `script_name` (03A4) con nombre `SWEET`, verificando la forma exacta del comando contra `data\script\main.scm`. Solo una vez por sesion de juego. Log `guest-launcher start name=sweet offset=%d counter=%d host=%d`; si no encuentra la etiqueta, log y no reintenta.

**Importante:** hasta ahora los invitados en scripts originales mandaban progreso -1, asi que `FindSweetMismatch` no los veia. Con el lanzador van a mandar su contador: en scripts originales ese chequeo no debe frenar al host (`FilterMissionStart`), porque el invitado puede ir adelantado. Con scripts coop sigue igual.

No debe cambiar: en coop el invitado sigue sin poder iniciar (`!IsScriptAuthority()` en `FilterMissionStart`), el bloqueo de `CMissionLock`, el blip de contacto (el invitado ya lo ve por espejo: no crees otro) ni la mision del host. Revisa que el lanzador corriendo en el invitado no duplique marcadores ni interfiera con `$onmission` espejado. Si encontras algo que lo haga inseguro, frena y explicalo en vez de forzarlo.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
