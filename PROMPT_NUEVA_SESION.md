Seguimos con el GTA San Andreas coop de historia (2–4 jugadores; fork de CoopAndreas, GPL-3.0; este repo `coopandreas-server`, remoto `origin` = GitHub `fv54367-dev/GTASAHISTORIAONLINE`). Hablo español rioplatense; respondé corto.

**Arrancá así:**
1. Verificá la carpeta y la rama con `git status -sb`. Leé `AGENTS.md` y el `TESTEAR_HOY.md` local, incluidas mis marcas sin commit. Para contexto técnico, consultá `ESTADO.md` y las referencias que pide `AGENTS.md`.
2. Trabajá sobre el pedido de este chat. Para retomar un fallo de juego, usá las `[N]` y `[I]` de `TESTEAR_HOY.md`. Un `[ ]` pide una prueba. Las tandas y `ESTADO.md` aportan contexto.
3. Escribís vos o subagentes Claude (Sonnet; Opus solo si te lo habilito), cada uno en su worktree, y revisás lo que devuelven contra `AGENTS.md`. Codex no se usa.
4. **Rama o `main`, y si se integra, lo decido yo:** si no lo dije, preguntame en una línea antes de empezar. Lo mismo con los PR del amigo.
5. Por cada punto: evidencia (gta-reversed, logs, capstone), arreglo de raíz y nativo, compilar, commit + push, `sh tools/publicar.sh`, cerrar GTA/server (leyendo sus logs antes) e instalar con `ACTUALIZAR.bat` `CANAL=main`. **No abras los GTA.** Anotá la prueba en `TESTEAR_HOY.md` (Drive se sube una vez al final) y decime qué quedó **sin probar**.
6. Si marco `[x]`, mantené la marca hasta que la confirmación quede registrada en Git en la rama donde vamos a seguir. Para volver a probar un caso confirmado, anotá qué cambió o cuál es el nuevo síntoma.
