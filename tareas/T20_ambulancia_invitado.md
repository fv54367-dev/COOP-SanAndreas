# Tarea T20_ambulancia_invitado: el paciente también sube a la ambulancia del invitado con scripts originales
Leé `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-08d.md`, `scm/scripts/AMBULAN.txt:2535-2560` y `client/src/CPatientHandover.cpp`.

## Investigación primero
En el modo coop, AMBULAN elige la ambulancia de cualquier participante. En el original solo mira al host. Determiná qué condiciones del script original deciden que un paciente suba (decodificá el `main.scm`/`script.img` original si hace falta) y si se puede cubrir con una regla acotada a `AMBULAN` en `FilterCompareFlag` más el traspaso existente de `CPatientHandover`. **Si requiere cambiar más que eso o puede romper el conteo de pacientes o el tiempo de la misión, no implementes**: explicalo en el resumen.

## Cambio, solo si es seguro
Interruptor `side-ambulance-any-player`, default true. Solo con `ms_bVanillaScripts`, en la secundaria coop `AMBULAN`. Log `rule-ambulance-guest`.

## Aceptación
Compilan client y server. Diff acotado + `INTERRUPTORES.md`/`TESTEAR_HOY.md` si se implementa. Resumen ≤8 líneas.
