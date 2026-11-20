# Tarea T32_marcador_pintura_drive_by: el cilindro grande de Drive-By no se dibuja
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. Sin interruptor nuevo: va con `CMissionSyncRules::IsEnabled()`.

## Problema (probado en juego, host e invitado)
Drive-By muestra un cilindro rojo enorme en el taller de pintura. Después de sacarse las estrellas, SWEET4 pregunta (`SWEET4.txt:2965-2966`) `Char.LocateInCar3D($scplayer, 2075.55, -1831.09, 12.21, 20.0, 20.0, 20.0, True)` y lo mismo en `1024.55, -1039.09, 35.21`, las dos con la bandera de dibujar encendida en el `main.scm` original (verificado en el binario). En el juego normal esa pregunta dura un solo frame, porque las estrellas se van adentro del taller. En coop las estrellas son de cada jugador: el host puede no tener estrellas y estar lejos, así que se pregunta todos los frames y el cilindro queda dibujado. El log del invitado lo confirma: `rx-area from=2055.6,-1851.1 to=2095.6,-1811.1 z=12.2` y `from=1004.6,-1059.1 to=1044.6,-1019.1 z=35.2`.

## Cambio
En `HighlightImportantArea_Hook` (`client/src/CMissionAreaMarkers.cpp`): si el script actual (`CommandHooks::GetCurrentScript()`) es `SWEET4` (sin distinguir mayúsculas), hay conexión, y el rectángulo coincide con una de esas dos áreas (centro y media anchura de 20 m, con tolerancia chica), no lo dibujes ni lo envíes. Usá una tabla chica.

El LOCATE sigue funcionando igual: solo se oculta el dibujo. El marcador chico de 4 m (`:2953`) no cambia, y tampoco los de otras misiones.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md` y en "Pendiente" de `ESTADO.md`. No instales ni toques los GTA. Resumen ≤4 líneas.
