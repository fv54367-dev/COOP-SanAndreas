# Tarea F1-scm — coopify.py: bloque nuevo de elección (opcode 1D29) y --rechoice

Herramienta: `E:\juegos\gtasacoopnuevo\tools\coopify.py` (Python, determinista). Scripts de misión: `E:\juegos\gtasacoopnuevo\coopandreas-server\scm\scripts\*.txt` (CRLF, 3.000-5.000 líneas: NO los leas enteros, usá búsquedas y leé fragmentos).
Diseño: `E:\juegos\gtasacoopnuevo\docs\DISENO_F_SECUNDARIAS.md` sección **2.6** (y 2.1 para entender el flujo). El opcode `1D29 Coop.ChooseSideMissionMode(coopName, title)` ya existe en C++ y en `sa_coop.json`/`sa_coop.db`.

## Reglas
- Solo tocar `tools\coopify.py` y los 22 scripts de `scm\scripts\` que hoy contienen `_COOP_CHOOSE` (`grep -l _COOP_CHOOSE`). Nada más. Sin commit.
- CRLF: leer y escribir con `open(..., newline='')` y conservar `\r\n`.
- Solo los 22 de la lista.
- Antes de modificar los scripts, hacé una copia de seguridad de cada uno en `E:\juegos\gtasacoopnuevo\tareas\backup_scm\`.

## Qué hacer
1. En `coopify.py`, cambiar el bloque que genera `--choice "Nombre"` por el bloque nuevo de la sección 2.6 (etiqueta `:XXX_COOP_CHOOSE`, `wait 0`, `if Coop.ChooseSideMissionMode('XXX', "Nombre")`, `goto_if_false`, y `Coop.CollectNetworkPlayersForTheMission()`), manteniendo el `script_name 'XXXSOLO'` y la ubicación después de `$onmission = 1`. Usá exactamente la sintaxis de Sanny Builder que ya usa coopify para los otros comandos `Coop.*` (mirá cómo escribe hoy `Coop.CollectNetworkPlayersForTheMission` y `Coop.IsHost`).
2. Agregar `--rechoice "Nombre"`: busca el bloque viejo (desde la línea `NNN@ = 0` previa al `if` con `Coop.IsHost()` hasta el `end` que sigue a la línea con `Modo individual")`), comprueba que contiene `_COOP_CHOOSE` y `EnableSyncingThisScript`, y lo reemplaza por el bloque nuevo. Si no matchea exacto, sale con error y no escribe nada.
3. Correr `--rechoice` sobre los 22 scripts. El "Nombre" de cada uno está en su bloque viejo (el texto del `AddChatMessage` / título que ya usa). Si alguno falla, no lo fuerces: anotalo.
4. Correr `python -I "E:\juegos\gtasacoopnuevo\tools\check_scm_inserts.py"` (detecta inserciones dentro de `if` y problemas de CRLF) y arreglar lo que marque en los scripts tocados.

## Respuesta
≤12 líneas: cuántos scripts se convirtieron, cuáles fallaron y por qué, resultado de check_scm_inserts. NO compiles con Sanny Builder (lo hace Claude).
