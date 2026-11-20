# Identidad visual propia del launcher CoopSA

Objetivo: barra de título oscura, fondo casi negro, marca CoopSA centrada y pie COOPSA 2026.
Archivos: launcher/src/main.rs, launcher/src/win32.rs, launcher/icon.ico, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Restricciones: ventana nativa y recursos locales. Mantener el arranque actual, las versiones y la licencia.
Casos borde: foco e inactividad de la ventana, escalado de pantalla, minimizar y cerrar.
En versiones de Windows sin color de título DWM, conservar los controles nativos.
Aceptación: compilar el launcher y verificar recursos y arranque local aislado.
Resumen de hasta 10 líneas.

Compila con xmake. Metadatos verificados. Enter inicia el ejecutable de prueba con --coop y cierra la ventana. Pendiente captura visual y clic en el juego instalado.
Pedido siguiente: usar el tercer logo adjunto. Archivos adicionales: launcher/coopsa-logo.bmp, launcher/build.rs y launcher/version.rc. Aceptación: bitmap incluido en el EXE; arranque aislado igual.
Compilado con el logo definitivo. Confirmado recurso bitmap 101 dentro de CoopSA.exe.
Pedido siguiente: usar el logo COOP SA en la barra de título, barra de tareas y Explorador. Archivos: launcher/icon.ico, launcher/src/main.rs y launcher/src/win32.rs. Verificar variantes de tamaño dentro del ejecutable.
Rust compila y el EXE contiene los nueve tamaños del logo como icono. Copia verificada en build/CoopSA-icono por estar en uso la salida de release.
Pedido siguiente: reemplazar launcher/icon.ico con la última imagen adjunta e instalar el EXE compilado en el GTA del usuario, conservando una copia del anterior.
Decisión final: icono COOP SA del tercer logo, sin logotipo de GTA. Recompilado y verificado en la instalación del usuario con respaldo.
Pedido siguiente: actualizar Organización y Nombre de producto a CoopSA en launcher/version.rc y server/version.rc. El servidor lleva descripción en español, versiones 0.4.43.0 / 0.4.43-alpha e icono CoopSA. Verificar los EXE instalados y conservar respaldo.
Launcher y servidor compilados e instalados. Metadatos de ambos EXE comprobados en la instalación.
Pedido siguiente: usar el logo adjunto de tres personas en el bitmap del launcher y el icono compartido por launcher y server. Archivos: launcher/coopsa-logo.bmp, launcher/icon.ico, launcher/src/main.rs y esta tanda. Recompilar ambos recursos e instalar con respaldo.
Logo nuevo comprobado como bitmap del launcher y como los nueve iconos de ambos EXE. Instalados y verificados por SHA-256 con respaldo.
