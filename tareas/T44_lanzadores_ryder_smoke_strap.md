# Tarea T44_lanzadores_ryder_smoke_strap: los invitados tambien tienen los lanzadores de Ryder, Smoke y OG Loc
Lee `tareas\COMUN_reglas.md` y T43 en `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Contexto
`ProcessGuestStoryLaunchers` (`client/src/CMissionSyncRules.cpp`) ya crea el lanzador de Sweet en el invitado y esta probado OK en juego (build 2026-11-09.6). Hay que generalizarlo a una tabla, sin cambiar lo que hace con Sweet.

## Datos (verificalos contra `data\script\main.scm` y `scripts\*.txt` de `_refs\project_gta_sa_coop\CoopAndreas\scm`)
| Lanzador | Firma 03A4 (offset en main.scm original) | Contador | Se termina solo en | Se habilita cuando |
|---|---|---|---|---|
| SWEET | 62090 | 1808 (cadena 0) | 9 | INTRO2: como hoy, progreso conocido del host |
| RYDER | 63143 | 1812 (cadena 1) | 3 | contador Sweet del host >= 5 (`SWEET4.txt:4012`, al pasar Drive-By) |
| SMOKE | 63492 | 1816 (cadena 2) | 4 | contador Sweet del host >= 4 (`SWEET2.txt:3428`, al pasar Nines and AKs) |
| STRAP | 63835 | 1820 (cadena 3) | 5 | contador Smoke del host >= 1 (`TWAR7.txt:3359`, al pasar OG Loc) |

Los offsets son solo para que confirmes la firma: en el codigo se sigue buscando la firma en memoria (unica coincidencia).

## Cambio
Dentro de `guest-story-launchers`:
1. Tabla con esos cuatro. El progreso del host sale de `m_nSweetProgress` para Sweet (como hoy) y de `m_anChainProgress[i]` para los otros; `CHAIN_UNKNOWN` = no hacer nada con esa fila.
2. Para cada fila, con las mismas condiciones generales de hoy: si la condicion de habilitacion se cumple, iguala el contador propio al del host si esta por debajo (nunca lo bajes), y si el lanzador no existe y el contador no llego al de terminacion, arrancalo. Misma espera de 5 s entre arranques, por fila. Logs `guest-launcher counter name=%s from=%d to=%d`, `guest-launcher start name=%s offset=%d counter=%d host=%d` y `label-unavailable name=%s matches=%d`.
3. La busqueda de cada firma se hace una vez por fila; si falla una fila, las otras siguen.
4. Revisa que `$onmission` siga siendo la condicion comun y que ningun lanzador nuevo cree blips propios ni pise `FilterMissionStart` (`IsLaunchBlocked`, `bAlone`, autoridad). Revisa las condiciones extra que cada lanzador lee (variables globales que el invitado no tenga): si alguna deja la mision imposible de iniciar, no la fuerces y explicalo en el resumen.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
