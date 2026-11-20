# TANDA 2026-11-15 — testeo del sábado noche (Claude + 5 Sonnet en worktrees)

Pedido del dueño (15-11, con 2 videos en `E:\juegos\gtasacoopnuevo\videos`). Todo "SIN PROBAR". Sin cambios de protocolo salvo que se anuncie acá.

## A. Historia temprana (Sonnet A) — `CMissionSyncRules` (sección INTRO1/INTRO2/SWEET1B), `CMirrorBlips`, `CNetworkStaticBlip`, `CCutsceneHidden`, `CScenePartner`/`CCutscenePartners` solo si hace falta
1. Big Smoke (INTRO1): el Voodoo de los Ballas se teletransporta en el invitado en la primera parte (antes de la autopista; ahí ya anda).
2. Al terminar Big Smoke y Ryder, a los invitados les queda el marcador rojo sobre CJ y el ícono del radar.
3. Tagging Up Turf: después de la primera cinemática un jugador ve 1 auto y otro ve 2 superpuestos.
4. Cinemática de partida nueva: cosas que deberían verse (cinta transportadora) quedan invisibles/transparentes.

## B. Autos de invitados y saltear viaje (Sonnet B) — `CMissionSyncRules` (Drive-Thru, OG Loc, Catalyst autos), `CMissionGuestActions`
1. Drive-Thru: el auto de invitados (Greenwood) aparece cerca después de la primera cinemática (como Drive-By), no cuando el host arranca.
2. Drive-Thru: después de la cinemática del Cluckin' Bell, el Greenwood aparece detrás del auto del host con los invitados adentro.
3. Drive-Thru: en el último marcador no bajar a los jugadores del auto.
4. OG Loc: al sacarlo de la cárcel el auto de invitados ya aparece; meterlos adentro (uno maneja, el resto acompaña).
5. Saltear viaje (reintento tras fallar, "trip skip"): al invitado no le aparece el auto ni lo saltea. General para todas las misiones con trip skip.
6. Catalyst: un invitado maneja un auto igual al de la misión (un solo auto extra para los invitados: maneja uno, el otro acompaña).

## C. Máquinas y cajas (Sonnet C) — `CMissionMachines`, `CMissionObjectMoves`, `CMissionGuestTrain`/sync de objetos de Catalyst
1. Robbing Uncle Sam (video 01-49-34): el invitado con la carretilla no puede meter la caja en el camión (choca, no entra, sin animación de entrega).
2. Robbing Uncle Sam: una carretilla por jugador.
3. Robbing Uncle Sam: al entregar el host, los invitados reciben su caja y se teletransportan: no tiene que pasar.
4. Catalyst: cajas de la caja del auto y del tren se toman como sólidas y se bugean en movimiento: sincronizar mejor.

## D. Lowrider y garaje (Sonnet D) — `CCoopLowrider`, `CGarageSync`, `CHighStakesRace`
1. César (video 00-51-22): con un invitado dentro del garaje, el host entra igual y no le dice "esperá afuera".
2. Al terminar César al invitado le desaparece su lowrider.
3. High Stakes Lowrider: aparece el HUD nativo de posición/tiempo ("4 /6 2:05"); solo debe quedar nuestra tabla de clasificación.

## E. HUD F2 y Home Invasion (Sonnet E) — archivo nuevo `CHudHideAll`, `CHomeInvasion`, `CCameraMirror` solo la parte de interiores
1. F2: ocultar TODO el HUD (juego, radar, textos, nombres de jugadores, nuestros HUD). Volver a apretar = mostrar. Interruptor.
2. Home Invasion: al entrar a la casa el invitado no toma la cámara del host.

## Casos borde (todos)
3-4 jugadores; uno en individual (F1) no recibe nada; desconexión o muerte a mitad; interruptor apagado = como antes.

## Hecho (compilado, sin probar; rev por Claude al integrar)
- A: `mirror-blips-clear-on-end` (blips espejados se borran al terminar la misión del host), `mission-car-overlap-dedupe` (auto ambiente propio debajo de una copia de auto de misión se borra; Claude: borrar fuera del recorrido y sin traspaso), cinta transportadora: `CCutsceneHidden` 2 m -> 0,75 m. Voodoo de Big Smoke: sin causa, no se tocó (ya está `mission-wide-car-on-screen`).
- B: `mission-drive-thru-first-car`, `mission-guest-car-board` (Drive-Thru 2.º auto y OG Loc con invitados adentro), `mission-trip-skip-guests` (`CGameLogic::SkipState` 0x96A8A0 / `SkipPosition` 0x96A8D4, gta-reversed), `mission-catalyst-guest-car` (Picador extra). Drive-Thru último marcador: sin causa (pide log).
- C: Robbing Uncle Sam modelo de caja 1558, entrega del invitado suma 127@ (`coop-robbing-guest-deliver`; índices tomados del .txt, verificar), sin tp en entregas del host; Catalyst `object-moving-nocollide`. Caja fantasma: sin causa.
- D: garaje simétrico, `lowrider-keep-car`, `high-stakes-hide-native-hud`.
- E: `hud-hide-all-f2` (`UI/CHudHideAll`), `camera-mirror-interior-guard`.
