# Launcher CoopSA en Rust + sin serial (GLM)

Objetivo: reemplazar el launcher C++ por uno en Rust + Win32 puro (una sola pantalla en español, fondo grafito, acento amarillo, nombre CoopSA y botón grande "JUGAR GTA SAN ANDREAS"; estilo tipo Rockstar Games Launcher con identidad propia). Y quitar de raíz el requisito de clave serial: `CLaunchManager` ya no valida `-id`/`-serial` (el server nunca los miró; `-id` solo servía para ese check). Flujo: abrir → Jugar, sin cuenta ni clave. Sin red, sin navegador embebido, sin video, sin procesos residentes (el launcher se cierra al lanzar el juego). Apertura rápida y bajo consumo, medidos contra el launcher actual.

Archivos:
- `client/src/CLaunchManager.cpp` y `.h`: borrar `Encrypt()`, el parseo de `-id`/`-serial` y la puerta que exigía serial válido (MessageBox + ExitProcess). Quedan `-connect`/`-nick`/`-autoload` intactos.
- `launcher/` (Rust nuevo, sin crates de crates.io, FFI Win32 a mano): `Cargo.toml`, `build.rs` (compila `version.rc` con rc.exe y lo linkea), `.cargo/config.toml` (CRT estático), `src/main.rs`, `src/win32.rs`, `src/launch.rs`, `CoopSA.exe.manifest` (embebido como RT_MANIFEST y también copiado al lado del exe), `version.rc` reescrito con los MISMOS metadatos (0.4.43.0 / 0.4.43-alpha, CoopSA (C) 2026, OriginalFilename CoopSA.exe, FileDescription "Modo cooperativo para GTA San Andreas", icono existente).
- Borrados: `launcher/src/main.cpp`, `launcher.h`, `pcid.h`, `registry.h`, `clipboard.h`, `resource.h`, `version.aps` (el launcher C++ y su puerta de serial desaparecen).
- `xmake.lua`: el target `launcher` pasa de msvc/C++ a wrapper de cargo (`xmake build launcher` sigue produciendo `build\windows\x86\release\CoopSA.exe` + `CoopSA.exe.manifest`; el exe Rust es x64 host con CRT estático, el resto del repo sigue x86).
- `.github/workflows/ci.yml`: instalar Rust en los dos jobs que compilan.
- `.gitignore`: `launcher/target/`.
- Docs: `README.md` (flujo sin serial), `docs/REQUISITOS.md` (requisitos del launcher), `ESTADO.md`, `TESTEAR_HOY.md` y esta tanda.
- No tocar: `.github/scripts/compilar_y_publicar.ps1` (sigue esperando los mismos archivos), `dist/ACTUALIZAR.bat` (el diff del renombre ya en el árbol), protocolo (`shared/`), server, scm.

Restricciones: CRLF en `.cpp`/`.h` (edición con Python `newline=''`; los `.rs`/`.toml` nuevos van en LF); `version.rc` en ASCII dentro del bloque 1252; sin crates externos (compila offline); CoopSA.exe sin DLLs de runtime (CRT estático); diff local sin commitear ni pushear; sin instalar sobre los GTA.

Criterio de aceptación:
- `xmake build client` y `xmake build -r launcher` compilan; server/testbot sin cambios.
- CoopSA.exe: VersionInfo idéntico al actual; manifiesto RT_MANIFEST embebido + externo; sin imports de ucrt/vcruntime; ventana única en español; 0 % CPU esperando el clic; el proceso sale al lanzar el juego.
- E2E en carpeta de prueba: clic simulado lanza un `gta_sa.exe` de mentira con `--coop` desde su carpeta y el launcher sale.
- RAM y tiempo de apertura medidos y anotados abajo contra el C++ actual.

Casos borde:
- Launcher viejo (`LaunchCoopAndreas.exe`) o `PROBAR_SOLO.bat` pasando `-id`/`-serial` contra el cliente nuevo: el cliente los ignora y arranca igual (compat hacia atrás); `--coop` sigue siendo lo que activa la inyección vía `eax.dll`, sin cambios.
- `gta_sa.exe` a mano sin `--coop`: vanilla, igual que hoy (el cliente ni se carga).
- 3–4 jugadores / individual (F1) / desconexiones: sin cambios; el serial era una puerta local por PC.
- Sin interruptor: la clave se quita de raíz (decisión del dueño 2026-11-11); el launcher C++ que la pedía se borra.
- PC de juego sin VS: CoopSA.exe es autónomo (CRT estático, manifiesto embebido); rc.exe/VS solo hace falta para compilar.

Mediciones (GLM, PC lucia, build local, caché caliente):
- Tres arranques con caché caliente y ventana oculta. Medianas C++ → Rust: inicio hasta WaitForInputIdle 78 → 65 ms, conjunto de trabajo 13,50 → 12,02 MiB, RAM privada 4,63 → 2,00 MiB. Con ventana abierta Rust marcó 0 ms de CPU durante 2 s en reposo.
- Enter inició el ejecutable de prueba con solo --coop, desde una carpeta con espacios, y cerró el launcher. VersionInfo y manifiesto embebido y externo verificados. Queda pendiente la prueba con el GTA real y con un paquete publicado.

- Codex verificó el ZIP local con la lógica de CI y ejecutó la etapa de extracción e instalación del actualizador contra una carpeta aislada. EXE, manifiesto y DLL coinciden por SHA-256 y los nombres anteriores quedaron en el respaldo. Sin publicar ni instalar en el GTA real.

Respondé con un resumen de hasta 10 líneas.
