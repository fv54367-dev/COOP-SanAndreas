# Renombre del launcher a CoopSA.exe (GLM)

Objetivo: renombrar el ejecutable y su manifiesto a CoopSA.exe / CoopSA.exe.manifest; en Propiedades, OriginalFilename=CoopSA.exe y FileDescription="Modo cooperativo para GTA San Andreas". Conservar versiones (0.4.43.0 / 0.4.43-alpha) y copyright (CoopSA (C) 2026) actuales.

Archivos: launcher/version.rc, xmake.lua, .github/scripts/compilar_y_publicar.ps1, dist/ACTUALIZAR.bat, README.md, TESTEAR_HOY.md, ESTADO.md y esta tanda.

Restricciones: no tocar logica del launcher, protocolo ni interruptores; conservar codificacion y CRLF de cada archivo (edicion con Python newline=''); texto nuevo del .bat sin acentos; el .rc queda en ASCII dentro del bloque 1252. No commitear: el diff queda local para revision.

Criterio de aceptacion: `xmake build -r launcher` produce build\windows\x86\release\CoopSA.exe y CoopSA.exe.manifest; VersionInfo del exe muestra FileVersion 0.4.43.0, ProductVersion 0.4.43-alpha, LegalCopyright "CoopSA (C) 2026", OriginalFilename CoopSA.exe y FileDescription "Modo cooperativo para GTA San Andreas"; sin referencias restantes a LaunchCoopAndreas en el repo (salvo historial/tandas).

Casos borde:
- Bat VIEJO + paquete nuevo: falla seguro en la validacion ("Al paquete le falta LaunchCoopAndreas.exe"), sin instalacion a medias; hace falta repartir una vez el ACTUALIZAR.bat nuevo.
- Bat NUEVO + paquete viejo (canal/PR aun sin renombre): tras descomprimir, si falta CoopSA.exe pero esta LaunchCoopAndreas.exe, se renombra en el stage y se instala como CoopSA.exe (igual el .manifest).
- Despues de instalar: si en la carpeta del GTA quedan LaunchCoopAndreas.exe/.manifest viejos, se mueven al respaldo (no se borran).
- El launcher localiza su manifiesto como <ruta del exe>.manifest (main.cpp:100): con el nombre nuevo funciona sin cambiar codigo.
- 2 a 4 jugadores, individual y desconexiones: sin cambios, es solo el arranque.
- Sin interruptor: el nombre es de compilacion, no hay comportamiento que volver atras.

Respondé con un resumen de hasta 10 líneas.
