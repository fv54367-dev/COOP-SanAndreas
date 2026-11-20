# TANDA 2026-11-19 — Cinemática de salida de casa (Big Smoke): los CJ invitados suben al auto

Pedido explícito del dueño (19-11). Feature de cinemática coop. Leer `AGENTS.md` (reglas, CRLF, gates de build/test),
`docs/MISIONES_MODO.md`, `tareas/COMUN_reglas.md`. Toca reglas de misión/cinemática → requiere crítica de diseño read-only
ANTES de codear (regla de protocolo de AGENTS.md), y los tres targets si toca protocolo.

## Comportamiento deseado (spec del dueño, textual)

En la cinemática de salida de casa de Big Smoke (intro, escena INTRO1A / el Greenwood):
- Cada CJ invitado se sube a un **asiento de pasajero LIBRE** del auto, aparte del NPC principal (Big Smoke) y aparte del CJ original (host).
- El invitado sube **prácticamente a la vez** que el CJ original (no después, no escondido, no corriendo detrás).
- Solo quedan **2 asientos de pasajeros libres** (el auto lleva al NPC principal + CJ original + 2 pasajeros). Con **más de 2 invitados**,
  los sobrantes **NO se muestran en pantalla** pero **ven la cinemática correctamente** (cámara/escena completa, sin pantalla negra).
- El modo F1 "Un solo CJ" (`cutscene-show-others=false`) sigue ocultando a todos los demás, como hoy.

Estado actual (19-11, marcado [N] en TESTEAR_HOY): el CJ invitado NO aparece subido al auto en esa cinemática.

## Sistemas existentes a reutilizar (verificados en el código)

- `client/src/CCutscenePartners.cpp` / `.h`: ya coloca invitados en escenas. Tiene `Seat(CVehicle*, int)` con los dummies nativos
  `GetFrontSeatPosn`/`GetBackSeatPosn` (`m_avDummyPos[4/5]`, gta-reversed VehicleModelInfo.h) y `coop::scene::HoldVehicleSeat(...)`
  para sentar un ped en un asiento. Maneja `entryRides`/`beforeMission` (auto de entrada por jugador) y el caso mission==15.
  Oculta invitados con `SetSceneHidden(...)`; el modo "Un solo CJ" se aplica en la línea ~451 (`ms_bCutsceneShowOthers`).
- `client/src/CScenePartner.cpp`: maneja los tracks del actor principal de la cinemática; comenta INTRO1A (el CJ vuela desde lejos
  en 12 de 148 escenas). Es la referencia de cómo se coloca/oculta al CJ en la intro.
- `client/src/CCoopMission.cpp`: `ShouldIncludeNetworkPlayer`, `IsScriptAuthority`, participantes de la misión.
- El auto de la cinemática es un objeto decorativo de la escena (CSPLAY); verificar cómo se crea y qué asientos ocupa el NPC principal.

## Diseño propuesto (a validar en la crítica)

1. Identificar la escena exacta de "salida de casa" (script/opcode de cinemática, id de misión, el auto decorativo y sus asientos).
   Verificar en `_refs/UndefinifiedGrove/main` el script de la intro y qué opcodes usan los actores (regla: comparar vanilla, no adivinar).
2. Para cada invitado incluido (`ShouldIncludeNetworkPlayer`), asignar un asiento de pasajero libre del auto de la escena, en orden
   (host/CJ original ya tiene el suyo; el NPC principal el suyo). Usar `Seat()` + `HoldVehicleSeat()` como hace hoy el sistema de escenas.
3. Sincronizar el momento de subir: el invitado entra al asiento en el mismo instante de la cinemática en que el CJ original sube
   (enganchar al track/opcode de "subir al auto", no un timer suelto). En el invitado, su propio CJ es el que sube; en el host, las copias
   de los invitados suben a la vez.
4. Más de 2 invitados: los que no tienen asiento libre se ocultan (`SetSceneHidden` con razón nueva, ej. `no-free-seat`) pero reciben
   la cinemática completa (no se les corta la escena ni queda pantalla negra). Verificar que el hide no les congela la cámara.
5. Interruptor nuevo (ej. `cutscene-intro-board`, default true) en CConfigLoader (default map + inline member + readBool +
   WritePrivateProfileString), documentado en INTERRUPTORES.md, para volver al comportamiento anterior.

## Archivos probables

`client/src/CCutscenePartners.cpp` (asignación de asientos + hide de sobrantes), posible `CScenePartner.cpp` (track de subir),
`CConfigLoader.cpp/.h` (interruptor), `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md`. Si toca protocolo: `shared/config.h` +
paquetes al FINAL de `COOP_PACKET_TYPES` + rebuild client/server/testbot + testbot `TODO PASS`.

## Aceptación

- 2 jugadores: en la salida de casa, el CJ invitado aparece subido a un asiento de pasajero libre, a la vez que el CJ original; ninguno corre detrás.
- 3-4 jugadores (JUGADORES=3/4 en PROBAR_SOLO.bat): 2 invitados suben, los sobrantes no se ven pero ven la cinemática completa sin pantalla negra.
- F1 "Un solo CJ": todos los demás ocultos, como hoy.
- El interruptor en false vuelve al comportamiento previo.
- Línea de diagnóstico que pruebe causa y arreglo en el próximo test del usuario (ej. `scene-intro-board player=%u seat=%d hidden=%d`).

## Casos borde / preguntas para la crítica

- ¿Qué asientos ocupa realmente el NPC principal (Big Smoke) en esa escena? ¿El auto es de 4 plazas (2 libres) o el NPC va de acompañante?
  Verificar en memoria/logs con los GTA abiertos antes de fijar el número de asientos libres.
- El CJ original "vuela desde lejos" en INTRO1A (CScenePartner.cpp:153): ¿el subir al auto es un track de posición o un opcode de entrar?
  Enganchar al mecanismo correcto para que el invitado suba "a la vez".
- Invitado que llega tarde o está en otro interior: no debe romper la escena ni quedar congelado.
- No duplicar el auto decorativo ni crear copias de CJ de más; reutilizar el ped de copia existente del sistema de escenas.
- La crítica debe verificar las premisas en memoria/logs en vivo si los GTA están abiertos (no solo leyendo código).

## Reglas de ejecución (AGENTS.md)

Tanda commiteada ANTES de codear → crítica read-only (spawn con repo+spec+hechos verificados+preguntas numeradas) → ajustar tanda →
implementar → (si protocolo) bump + 3 targets + testbot → cross-review del diff → compilar → instalar (actualizar_copiar.ps1) →
commit por punto. La IA NO juega: compila e instala, el usuario prueba con PROBAR_SOLO.bat. Reportar "compilado, sin probar" hasta confirmar.