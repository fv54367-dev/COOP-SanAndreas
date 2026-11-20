# Reglas comunes de las tareas (leer primero)

Reglas canónicas del proyecto: `AGENTS.md` (raíz del repo). Repo: `E:\juegos\gtasacoopnuevo\coopandreas-server` (git local). C++ con CRLF: editar con Python `open(..., newline='')` preservando `\r\n`; nunca `sed -i`. No tocar `D:\GTA SERVER`. Compilar `xmake build client` y `xmake build server` (si el build tarda ~1 s usá `-r`). No intentes ejecutar el juego. Comentarios cortos en inglés, estilo del archivo. Inline asm: las constantes `constexpr` no se pueden usar dentro de `__asm` (usar literales). Respondé con un resumen de ≤10 líneas: qué cambiaste y qué dejaste sin hacer.

Informe base para misiones: `tareas\archivo\AUDIT_MISIONES_informe.md` (trae `archivo:línea`). Informe del robo de vehículos: `tareas\archivo\AUDIT_JACK_informe.md`.
