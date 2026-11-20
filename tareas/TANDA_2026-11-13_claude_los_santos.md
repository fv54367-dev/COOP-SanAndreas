# TANDA 2026-11-13 — Claude: misiones de Los Santos

Pedido del dueño: adaptar todas las misiones de Los Santos para publicar. Análisis: 4 subagentes Sonnet de solo lectura (INTRO1/INTRO2/SWEET1/SWEET1B, SWEET2/CRASH4/CRASH1/DRUGS3, MUSIC2/MUSIC3/MUSIC5, DRUGS4/LA1FIN2/TWAR7/HOODS5). Escribió Claude (Opus) en main; todo SIN PROBAR y sin revisión cruzada. Sin cambio de protocolo (0.4.49).

## Hecho (un commit por punto)
1. `mission-pickup-weapon-share` (`CMirrorPickups`): el arma de un pickup de misión llega a todos. Causa: CRASH1 espera `HAS_CHAR_GOT_WEAPON($scplayer, extintor)` y si el invitado agarraba la copia el host quedaba sin arma (traba). `IsPickUpPickedUp` NO se usa (consume la entrada del anillo que lee el script): se lee `aPickUpsCollected`.
2. `coop-madd-dogg` (bici 510 por invitado en la huida) y `coop-management-issues` (asiento libre del Elegant + 1 auto compartido; MUSIC3 sale de `ProcessMissionBoarding`).
3. `coop-gray-imports`: 00F2 entre host y jefe (200/60/40 m) cuenta al jugador más cercano. Extendido a TWAR7 250 m (`coop-og-loc-escape`).
4. Lanzadores de invitados CRASH, CESAR, LA1FIN, BCRASH (`s_aGuestLaunchers`); `SET_INT_STAT` replicado solo desde LA1FIN2.
5. `coop-green-sabre`: 036A del host al Bravura -> `ParkSharedGuestCar("green-sabre")`.
6. `coop-car-gunners` (`CMissionCarGunners`, nuevo): invitado enganchado al auto al que está enganchado el dueño (DRUGS4).
7. `mission-boarding-shared-car`: los que sobran en `PutPlayersInCar` comparten 1 auto (regla del 11-11), salvo que la misión ya haya estacionado autos.

8. Auditoría (Sonnet) de Drive-By/Los Sepulcros/Running Dog/César: garaje de César primero para el dueño + aviso del lowrider; auto de vuelta de Los Sepulcros al salir el host con el Admiral (antes se marcaba hecho con 0 invitados cerca); Tec9 a todos; no se estacionan autos para quien ya maneja uno.
9. `mission-wait-pause-timer` (`CMissionHints`): el reloj en pantalla se frena mientras la autoridad espera jugadores.
- Big Smoke y Cleaning the Hood: el dueño pidió no tocarlas (andan bien).

## Casos borde
- 3-4 jugadores: autos compartidos de 3; artilleros en filas de a 2 detrás del dueño.
- F1/abandono: `ShouldIncludeNetworkPlayer` los deja afuera en todo lo nuevo.
- Desconexión del dueño: `CMissionCarGunners` suelta al invitado al perder el objetivo.
- Interior distinto: todo exige misma área.
- Interruptores apagados: comportamiento anterior.

## Pendiente (de los informes, sin hacer)
- INTRO1: la bici del invitado no se repone si la pierde. SWEET1B: entrar a la casa de crack separados (probar antes de tocar).
- CRASH4: la guerra de pandillas solo arranca con el host en GLN1 (`IsInInfoZone` del host).
- TWAR7: la velocidad de la grabación de Freddy usa solo la distancia del host (el script viejo usaba la más cercana).
- MUSIC5 y en general: los enemigos solo persiguen al host (`mission-hunters=false` por pedido del dueño).
- LA1FIN2: el reloj de la vida de Sweet sigue mientras el host espera invitados en el marcador. Armas: el host las pierde en Badlands y el invitado no.
- MUSIC3 muelle: con un invitado sentado en el Elegant la escena espera a que se baje.
- DRUGS3 sigue entera en `requiresHostCar` (falta tabla de tramos).
