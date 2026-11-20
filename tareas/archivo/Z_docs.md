# Tarea Z_docs: limpiar y compactar la documentación

Carpeta: `E:\juegos\gtasacoopnuevo` (solo escribir ahí; NUNCA tocar `D:\GTA SERVER` ni ningún repo de GitHub). No tocar código (`coopandreas-server\`), solo `.md`.

## Objetivo
Que una sesión nueva de Claude arranque con poco contexto y sepa qué hacer. Quitar info vieja, repetida o irrelevante.

## Estado real de hoy (2026-10-19) para reflejar
- Etapas A–G y H2 hechas en `coopandreas-server` (git local, sin remotes). Commits finales: `7f95af3` (H2 arreglos auditoría), `8b14cef` (G robustez: log a archivo, validación de paquetes, reconexión automática, mensajes de conexión). Todo compilado, testbot TODO PASS, instalado en `GTA_JUGADOR1`, `GTA_JUGADOR2` y `server`. SCM MAIN idéntico. **Todo sin probar en juego** (lo prueba el usuario con `PROBAR_SOLO.bat` siguiendo `TESTEAR_HOY.md`).
- Quedó renombrado `TESTEAR_MANANA.md` -> `TESTEAR_HOY.md`.
- Hay parches por etapa en `parches\` (A, B, C, D, E, F, H2, G; cada carpeta con `.patch` ordenados) para llevar de a poco los cambios al repo público `fv54367-dev/coopandreas-server` en una copia aparte, sin tocar ese repo desde acá. Orden recomendado: A, B (+testbot), y el resto solo después de probar en juego. `scm/main.scm` es binario y choca: recompilar con SB4 + `check_main_layout.py`.
- Pendiente de revisión a fondo: etapas C–F2, H2 y G están "SIN REVISAR" (solo revisión parcial + auditoría). Sigue: que el usuario pruebe, corregir lo que reporte, tag `probada-AAAA-MM-DD` al confirmar.
- Limitaciones conocidas y "fase 3" de secundarias: ver `PENDIENTES_SIGUIENTE_SESION.md` (migrar lo vigente).

## Qué hacer
1. Leé todos los `.md` de la raíz (CLAUDE.md, ESTADO.md, ESTADO_NOCHE.md, PLAN_NOCHE.md, PENDIENTES_SIGUIENTE_SESION.md, TAREAS_PENDIENTES.md, TESTEAR.md, TESTEAR_HOY.md, WORKFLOW.md, INTERRUPTORES.md, AUDITORIA_NOCHE.md, DISENO_*.md, REFERENCIAS_OPENSOURCE.md).
2. Hacer un **backup** de todos en `docs_viejos\` (copia tal cual) antes de tocar nada.
3. Reescribir, más cortos y sin repetir entre archivos:
   - `CLAUDE.md`: mantener la regla de oro, trampas, rutas y decisiones fijas (no perder ninguna trampa ni decisión). Quitar lo desactualizado (p.ej. "Última probada", "Estado y siguiente paso" -> apuntar a `ESTADO.md`). Agregar arriba un bloque "Para arrancar una sesión nueva: leé solo este archivo y `ESTADO.md`". ≤ 100 líneas.
   - `ESTADO.md`: una pantalla (≤ 40 líneas): qué está hecho (tabla etapa -> commit), qué sigue (lista ordenada), limitaciones. Es el único doc de estado; absorber `ESTADO_NOCHE.md`, `PENDIENTES_SIGUIENTE_SESION.md` y `TAREAS_PENDIENTES.md` (borrarlos al migrar lo vigente; ya están en `docs_viejos`).
   - `TESTEAR.md`: unir `TESTEAR.md` + `TESTEAR_HOY.md` en un solo `TESTEAR.md` (checklist de pruebas concreta, agrupada por etapa, ≤ 80 líneas). Borrar `TESTEAR_HOY.md` (el usuario lo quiere llamar así: si preferís, dejá el archivo final como `TESTEAR_HOY.md` y borrá `TESTEAR.md`; elegí UNO solo y ajustá las referencias).
   - `WORKFLOW.md`: dejar cómo correr tareas (`correr_tareas.py`, ruta absoluta), compilar SCM, testbot, `ACTUALIZAR_GTA_SERVER.bat`, parches. Quitar historia.
   - `PLAN_NOCHE.md`: borrar si ya es historia (migrar solo reglas vigentes a WORKFLOW).
   - `AUDITORIA_NOCHE.md`: reducir a lista de ítems con estado (arreglado en H2/G, pendiente). Los pendientes van a ESTADO.
   - `DISENO_*.md` y `REFERENCIAS_OPENSOURCE.md`: dejar en `docs\` (moverlos), recortados a lo útil (≤ 120 líneas cada uno); en CLAUDE.md solo una línea que diga cuándo leerlos.
   - `INTERRUPTORES.md`: dejar si es referencia vigente, recortado.
   - Crear `PORTAR_A_GITHUB.md` (≤ 40 líneas): cómo llevar los parches de `parches\` a una copia aparte del repo público, en qué orden, trampas (main.scm binario, CRLF, protocolo 0.4.0-alpha), sin push desde este proyecto.
4. Actualizar todas las referencias cruzadas entre archivos (nombres y rutas nuevas). No dejar enlaces a archivos borrados.
5. Mantener español rioplatense, sin relleno. No inventar hechos: si algo no está en los docs, no lo agregues.

## Criterio de aceptación
- Total de líneas de los `.md` activos bastante menor que antes (reportá antes/después).
- Backup completo en `docs_viejos\`.
- Ninguna trampa/decisión fija de `CLAUDE.md` perdida.
- Respondé con un resumen de ≤10 líneas.
