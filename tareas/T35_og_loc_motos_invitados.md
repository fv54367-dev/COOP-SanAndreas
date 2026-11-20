# Tarea T35_og_loc_motos_invitados: OG Loc da motos extra a los invitados
Lee `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo salvo que sea imprescindible (si lo es, frena y explica). No toques `main.scm`.

## Pedido
En OG Loc (mision 27, script `TWAR7`), cuando aparece la moto del host para perseguir a Freddy, tienen que aparecer motos extra para los invitados: una moto por cada 2 invitados, redondeando para arriba (1 o 2 invitados = 1 moto; 3 invitados = 2 motos).

## Datos
- La moto del host es `36@ = Car.Create(461, 2471.0, -1285.1, 28.3)` (`_refs\project_gta_sa_coop\CoopAndreas\scm\scripts\TWAR7.txt:915`). La de Freddy es `35@`, modelo igual en otra posicion: no la confundas. Nunca crees la extra sobre la moto de Freddy.
- Ya existe `CCommandPutNetworkPlayersInCar::ParkCopiesNextTo` (una copia por invitado, creada en el receptor con blip azul y limpieza en `ProcessParkedCopies`), disparada desde `OnCarBlipAdded`, pero en TWAR7 el blip del host solo aparece si se baja (`TWAR7.txt:3594`), demasiado tarde.

## Cambio
Detras de un interruptor nuevo `mission-og-loc-bikes` (default true, documentado en `INTERRUPTORES.md`):
1. Solo en la autoridad, en `CMissionSyncRules::OnCommandExecuted`: cuando `TWAR7` ejecuta `CREATE_CAR` (0x00A5) de modelo 461 en esa posicion (tolerancia 3 m), estacionar las motos extra al lado de esa moto. Verifica en gta-reversed donde queda el handle de salida en `ScriptParams`.
2. Cantidad = (invitados incluidos + 1) / 2. Verifica primero si un vehiculo creado con `CREATE_CAR` en el handler del receptor llega a los demas clientes como vehiculo de red. Si llega, agrega un parametro a `ParkCopiesNextTo` para mandar el paquete solo a uno de cada 2 invitados. Si no llega, el segundo invitado no veria la moto: en ese caso crea las motos en la autoridad (quedan sincronizadas como cualquier auto del host), con blip para los invitados y marcadas como no necesarias al terminar la mision, y explica la eleccion en el resumen.
3. Log `rule-og-loc-bikes guests=%d bikes=%d`.

No cambies INTRO1 ni el comportamiento de `mission-guest-car-copies`. Los chequeos del script sobre `36@` y `$scplayer` siguen igual.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Una linea simple en `TESTEAR_HOY.md`, actualiza `docs/MISIONES_MODO.md` y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
