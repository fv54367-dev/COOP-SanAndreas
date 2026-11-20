# TANDA 2026-11-13 — Claude: Badlands (BCRASH1) en coop

Pedido del dueño: adaptar Badlands al coop. Escribe Claude (Opus) en main. Sin cambio de protocolo (0.4.50).

## Ya estaba
- Lanzador de invitados BCRASH (`s_aGuestLaunchers`), cámara para todos (`mission-player-equipment` replica `GIVE_WEAPON_TO_CHAR`/`SET_CHAR_AMMO` del host), fotos del invitado (`coop-photos`, T65).

## Archivos
`client/src/CMissionSyncRules.cpp`, `client/src/CMissionMachines.cpp`, `client/src/CConfigLoader.h/.cpp`, `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md`, `docs/MISIONES_HECHAS.md`.

## Hecho
1. `vehicle-damage-any-player` (genérico): el daño que el invitado reporta a un auto de misión (`GUEST_MACHINE_DAMAGE`, ya existía) se aplicaba con `InflictDamage` (0x6D7C90), que llama primero a `CanVehicleBeDamaged` (0x6D1280, gta-reversed Vehicle.cpp): con `SET_CAR_ONLY_DAMAGED_BY_PLAYER` (bInvulnerable) rechaza a todo el que no sea `FindPlayerPed()`. Ahora se levanta el flag solo alrededor de esa llamada (igual que `ped-damage-any-player` con los NPC). En Badlands: el Washington del testigo (322@) recibe tiros y choques del invitado.
2. `coop-badlands` (BCRASH1, en el dueño): cuentan todos los jugadores de la misión en `IS_LINE_OF_SIGHT_CLEAR` desde el host al testigo (15/30 s sin verlo = falla), `LOCATE_CHAR_ANY_MEANS_CHAR_3D` host-testigo 100/25 m (se escapó), `_2D` 10 m (sale corriendo) y `LOCATE_CHAR_ANY_MEANS_3D` 100x100x30 alrededor del cadáver (te fuiste sin la foto). Los de 4-5 m quedan del host.

## Casos borde
- 3-4 jugadores: alcanza con uno que lo vea / esté cerca.
- F1 o abandono: `ShouldIncludeNetworkPlayer` los deja afuera; muertos y otra área no cuentan.
- Invitado que se desconecta: deja de contar; vuelve la regla del host.
- Interruptores apagados: comportamiento anterior.

## Sin hacer
- Si el host se queda sin rollo (`GET_AMMO_IN_CHAR_WEAPON` del host = 0) el script pide ir a buscar otra cámara aunque el invitado tenga la suya (la foto del invitado igual cuenta).
