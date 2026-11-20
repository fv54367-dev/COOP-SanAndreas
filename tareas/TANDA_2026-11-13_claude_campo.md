# TANDA 2026-11-13 — Claude: misiones del campo (Badlands) en coop

Pedido del dueño: adaptar todas las misiones del campo. Análisis: 4 subagentes Sonnet de solo lectura (CATALIN/CAT1/CAT2, CAT3/CAT4/CATCUT, TRUTH1/TRUTH2/BCESAR4, lanzadores). Escribió Claude (Opus) en main; sin probar y sin revisión cruzada. Sin cambio de protocolo (0.4.50). Badlands (BCRASH1) está en `TANDA_2026-11-13_claude_badlands.md`.

## Archivos
`client/src/CMissionSyncRules.cpp`, `client/src/CMissionMode.cpp`, `client/src/Commands/Commands/CCommandPutNetworkPlayersInCar.cpp`, `client/src/CConfigLoader.h/.cpp`, docs.

## Hecho
1. **Contador de Catalina mal leído:** `s_aChains` usaba el offset 1956 (variable del salto de depuración de CATALIN); el real es `$cat_counter` = 256 (verificado: `cat_counter += 1` 4 veces en el main.scm original). Antes el progreso de Catalina se mandaba siempre en 0.
2. **Lanzadores de invitados** `CAT` (Catalina, después de Badlands) y `TRU` (The Truth, después de la primera cita). CAT arranca 7 bytes antes de su `SCRIPT_NAME` (`$load_and_launch_catalina_mission = -1`; si no, lanzaba Local Liquor Store solo). Con el contador de Catalina copiado del host ≥ 2 se pone `$flag_trailor_cutscene` (8652) = 1 (King in Exile ya pasó en el host).
3. `coop-countryside`, autos de invitados (aparecen con su ícono y cada uno se sube solo): quad 471 cada 2 invitados cuando el host maneja uno (CAT1), motos 523 cada 2 junto a la del host detrás del banco (CAT2), 1 auto compartido de 4 puertas junto al camión cisterna (CAT3), a la cosechadora (TRUTH1) y al camper (TRUTH2).
4. `coop-countryside`, condiciones: en Body Harvest la cosechadora la puede manejar un invitado (`IS_CHAR_IN_CAR`/`IS_CHAR_SITTING_IN_CAR` host + 532); en Are You Going to San Fierro? una planta con fuego encima cuenta como quemada (el fuego del invitado llega al host por `CFireSync`; `IS_CLOSEST_OBJECT_OF_TYPE_SMASHED_OR_DAMAGED` radio 1).

## Casos borde
- 3-4 jugadores: quads/motos de a 2, auto compartido de 3.
- F1, abandono o muerto: fuera del reparto y de las condiciones (`ShouldIncludeNetworkPlayer`).
- Interruptor apagado: comportamiento anterior. Guardado: los contadores copiados del host no se guardan (`GenericSave_OwnProgress`); `$flag_trailor_cutscene` sí queda.

## Sin hacer (de los informes)
- **Carreras de César del campo (Wu Zi Mu, Farewell My Love; BCESAR, CPRACE selección 7/8):** el invitado solo mira. Hace falta extender `CHighStakesRace` a esas selecciones y un lanzador BCESAR (cadena nueva = protocolo). Sin eso, el invitado no avanza esa cadena y su parte 2 de The Truth no aparece.
- CAT4: los satchels que cuentan son los del host (si el host gasta los 10 sin romper la caja, falla aunque el invitado tenga). Las 4 estrellas son solo del host.
- CAT2: intimidar a los rehenes cuenta al invitado que apunta (T63), pero el host tiene que tener un arma en la mano.
- CATALIN/CAT1: Catalina solo vuelve al grupo con el host cerca (10 m).
- TRUTH2: si el host se aleja más de 200 m del camper, falla.
