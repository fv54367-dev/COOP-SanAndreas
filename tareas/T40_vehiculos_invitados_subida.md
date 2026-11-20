# Tarea T40_vehiculos_invitados_subida: vehiculos para invitados, bien orientados y con subida automatica
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md` (T35, T37, T38). No toques `main.scm`. Solo esta tarea puede tocar protocolo: si cambia el formato o el rango serializado de `PutInVehicleScripted` (mira `serialize_int` del campo `seat`), subi `COOPANDREAS_VERSION` a 0.4.20-alpha.

## Problemas (probados en juego)
- Drive-Thru: el auto aparece bien, pero los invitados no se suben solos.
- OG Loc: la moto extra sale mal orientada y el invitado no se sube. `ParkCopiesNextTo` manda `pVehicle->GetHeading()` en radianes y el receptor lo usa en `SET_CAR_HEADING`, que va en grados; ademas T35 dispara en `CREATE_CAR`, antes de `Car.SetHeading(36@, 272.5)`.
- OG Loc: falta un auto para los invitados despues de sacar a OG Loc de la prision (el auto del host `37@`, modelo 466, va lleno con Smoke, Sweet y OG Loc).

## Cambio
Un solo camino generico en `CCommandPutNetworkPlayersInCar`: vehiculos creados por la autoridad (visibles para todos), con blip por `CMirrorBlips` y liberados al terminar la mision como hace T37. Generaliza `FindDriveThruCarPosition` con parametros (tabla de offsets, largo/ancho, punto a evitar opcional), sin relajar ninguna comprobacion de suelo, linea de vista ni colision.

Subida: una cola en la autoridad procesada cada frame. Espera a que el vehiculo tenga id de red, y manda a cada invitado incluido su lugar: el primero maneja y el resto va de pasajero. Si hace falta, agrega un valor especial de `seat` para "conductor" (`-3`, junto a `PARKED_COPY_SEAT`). El handler de `PacketHandlers/scripts.cpp` debe warpear con `WARP_CHAR_INTO_CAR` sin caer en la rama de "crear un auto propio". Espera a que el invitado este a pie (el warp del host lo baja del auto de la mision un instante despues) con un limite de 8 s desde que se cumple su condicion. Si el vehiculo todavia no existe en el receptor, reintenta un rato; si no aparece, no crees nada: queda el blip. Respeta `ShouldIncludeNetworkPlayer`, F1, muertos y otra area.

Casos, cada uno una vez por mision, con log `rule-guest-vehicle case=%s guests=%d vehicles=%d`:
1. **Drive-Thru** (reemplaza la creacion de T37/T38; mismo disparador 0175, mismo punto a evitar): 1 auto para hasta 3 invitados, subida apenas se pueda.
2. **OG Loc, motos** (`TWAR7`, interruptor `mission-og-loc-bikes`): deja de usar `ParkCopiesNextTo`. Dispara en `SET_CAR_HEADING` (0x0175) de la moto `36@` (modelo 461, 3 m de 2471.0, -1285.1, heading 272.5). Moto cada 2 invitados, redondeando para arriba, al costado o atras de la del host, misma orientacion. Subida cuando el host ya este sobre `36@` (escena o salteo), con limite de 90 s: primero maneja, segundo pasajero.
3. **OG Loc, auto tras la prision** (`TWAR7`, interruptor nuevo `mission-og-loc-guest-car`, default true, en `INTERRUPTORES.md`): dispara cuando el script sube a `$mc_strap` al auto del host, `WARP_CHAR_INTO_CAR_AS_PASSENGER` sobre modelo 466 a menos de 120 m de 1543.2, -1687.0. Verifica cuales son los caminos reales en `TWAR7.txt:217` y `:524`. 1 auto, orientado como el del host, subida apenas se pueda. Si `37@` todavia no tiene su heading final, espera a tenerlo.

`INTRO1` y `ParkCopiesNextTo` por defecto siguen igual, pero corrigi la conversion de radianes a grados del heading en el emisor o el receptor, una sola vez.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple por mision en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas, y deci si subiste el protocolo.
