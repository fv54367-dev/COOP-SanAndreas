# Tanda 2026-11-16 (Claude): launcher tanda 3, actualizador dentro del launcher

Objetivo: el botón de actualización de CoopSA instala él mismo el canal (sin ACTUALIZAR.bat): baja `coopandreas.zip` + `manifest.json` del release público, verifica el sha256, respalda en `_backup_mod`, reemplaza los archivos (el `CoopSA.exe` en uso se renombra a `.old` y se borra al próximo arranque) y se reabre.

Archivos: `launcher/src/update.rs`, `launcher/src/main.rs`, `launcher/src/win32.rs`. Nada del cliente, server ni protocolo.

Restricciones: CRLF; sin crates externos (curl.exe y el tar.exe de System32 que ya trae Windows 10+); ACTUALIZAR.bat sigue en el paquete y es el plan B (si falla la instalación, el launcher ofrece abrirlo).

Casos borde:
- GTA o server abiertos: no toca nada y lo dice.
- Descarga cortada o paquete a medio publicar: el hash no coincide, no se instala nada.
- Falla copiando a mitad: vuelve lo copiado a la versión del respaldo (también el CoopSA.exe renombrado).
- Sin `tar.exe` (Windows viejo): error y plan B ACTUALIZAR.bat.
- Canal de PR/todo: se instala el mismo canal que mostró el chequeo.
- 3-4 jugadores / F1 / interiores: no aplica (solo launcher).

Fuera de esta tanda (queda en "Qué sigue"): migrar los nombres viejos (`CoopAndreas\`, `coopandreas.ini`, `CoopAndreasSA.dll`...), que toca cliente y proxy.

Hecho (Claude, sin probar desde la ventana):
- `update.rs`: `install()` (curl + sha256 propio + `tar.exe` de System32, respaldo, copia con vuelta atrás, eax/eax_orig, LaunchCoopAndreas al respaldo, Open Limit Adjuster, `CoopAndreas_version.txt`, ACTUALIZAR.bat nuevo), `start_install`, `restart`, `remove_old` (borra `CoopSA.exe.old` al arrancar).
- `main.rs`: la flecha instala en un hilo (texto de progreso al lado del botón, no se puede cerrar mientras), al terminar cartel + reabrir; si falla, ofrece ACTUALIZAR.bat.
- Probado: `cargo test` (sha256 contra vectores conocidos) e `install_main` (ignorado por defecto) instalando el canal main real en una carpeta falsa, dos veces seguidas: archivos, respaldo, eax_orig y OLA bien.
