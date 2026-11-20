# Tanda 2026-11-16 (Claude) — launcher: elegir la carpeta de partidas

**Objetivo:** en la pantalla PARTIDAS del launcher, un botón (carpeta, arriba a la izquierda) para elegir dónde están las partidas guardadas. Escribe `user-dir` en `<juego>\CoopAndreas\coopandreas.ini`, la misma clave que ya usa el cliente (`CTestMode::EarlyInit`). No cambia el cliente ni el protocolo.

**Archivos:** `launcher/src/saves.rs`, `launcher/src/main.rs`, `launcher/src/win32.rs`; docs: `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md`.

**Casos borde:**
- Elegir `Documentos\GTA San Andreas User Files` (sin modo prueba) borra la clave: vuelve a lo de siempre.
- La carpeta nueva sin `gta_sa.set` / `coopandreas.ini`: se copian de la carpeta anterior (sin pisar). Las partidas no se mueven.
- Ruta con letras no ASCII (ñ, tildes) o de 200+ letras: se rechaza con aviso (el juego usa rutas ANSI y búferes chicos).
- No se puede elegir `<juego>\CoopAndreas` (ahí está el ini del launcher/cliente local).
- 3-4 jugadores / desconexión / interiores: no aplica (es local a cada PC, antes de abrir el juego).
- "Interruptor": es la clave `user-dir` en sí; sin elegir nada todo queda igual que antes.

**Estado:** hecho, sin probar en juego.
