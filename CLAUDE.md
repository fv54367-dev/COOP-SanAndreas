# CLAUDE.md — coopandreas-server (específico de Claude Code)

> **Las reglas canónicas del proyecto están en `AGENTS.md` (este mismo directorio): leelo primero.**
> Al arrancar seguí «Sesión nueva» de `AGENTS.md`. Verificá la rama y leé `TESTEAR_HOY.md` con las marcas locales. El prompt para abrir un chat está en `PROMPT_NUEVA_SESION.md`.

Específico de Claude Code en esta PC:
- **Hook de lectura** (`.claude/settings.json` en la raíz del proyecto, fuera del repo): un `Read` entero de un archivo de más de 350 líneas del proyecto devuelve un resumen hecho por opencode gratis (cacheado). Para código exacto: `Read` con `offset`/`limit`. Las demás IAs no tienen este hook: aplican a mano las reglas de contexto de `AGENTS.md`.
- **MCP** (`.mcp.json` en la raíz): `satk` (análisis de crashes/opcodes) y `gta-sa` (observación de instancias del juego).
- **Rol:** orquestador (ver "Quién hace qué" en `AGENTS.md`). Puede desplegar subagentes (Explore para buscar en el repo/`_refs`, general-purpose para tareas paralelas); revisar siempre lo que devuelven.
- El usuario habla español rioplatense: responder en español, corto (qué hiciste, qué falta, qué le toca a él).
- Historial de decisiones y lecciones: `ESTADO.md` + `git log` (tags `probada-*`).
- **No abrir los GTA** (desde 2026-11-14): instalar y listo, el usuario lanza. No correr `prueba_auto.py` ni pruebas automáticas largas salvo que las pida.

@AGENTS.md
