# TANDA 2026-11-20 — Catalyst (RYDER3): el invitado maneja el Picador de Ryder

## Pedido del usuario
- Sólo el **host** lanza cajas (él es el que va arriba del vagón, como en el original).
- **Un invitado maneja el Picador donde está parado Ryder** (el que atrapa las cajas), en lugar de que
  ese auto lo conduzca la grabación del script.
- **Otro auto igual al de Ryder** para los demás invitados.
- La sincronización de las cajas sigue 1 a 1 (un `objectid` por objeto del dueño, `CObjectSync`).

## Cómo funciona hoy (verificado en el repo)
- `RYDER3.txt:210` crea el Picador `108@` (modelo 600, color 84). `:222` sienta a Ryder de acompañante.
- `:1507-1509` `Car.StartPlayback(108@, 486)` + `Car.PausePlayback`; `:1546-1549` lo despausa.
  O sea: en la fase de atrapar, **el auto se maneja solo** por la grabación 486.
- `:1588` el CJ del dueño se une al vagón (`ATTACH_CHAR_TO_CAR(163@, 0, -8, 0.9)`) y desde ahí lanza.
- `:1492` Ryder se crea de nuevo y se une al auto (`ATTACH_CHAR_TO_CAR(108@, 0, -0.85, 0.65)`).
- `:1744-1761` Ryder atrapa: por cada una de las 10 cajas, si está a ≤1 m de su offset de mano,
    colisión off + `TASK_PICK_UP_OBJECT("CATCH_BOX")`. **Ahí sube el contador** (`$collected_guns`).
- Mod hoy: el invitado también sube al vagón (`CMissionGuestActions::AttachToTrain`) y lanza cajas
  (`THROW` → el dueño ejecuta `HostThrow`, `CMissionGuestActions.cpp:296-328`). Con
  `mission-catalyst-guest-car=true` los invitados tienen un Picador extra (600) estacionado al lado
  (`CMissionRulesGuestCars.cpp:498-511`).

## Diseño nuevo (interruptor `mission-catalyst-ryder-car`, default true)
1. **Sólo el host lanza.** `CMissionGuestActions` deja de abrir sesión para RYDER3: sin sesión no hay
   `INPUT`/`THROW` del invitado ni enganche al vagón (`AttachToTrain` queda inerte porque
   `session.pressed` nunca llega a true). Los otros specs (HEIST2, WUZI2) no cambian.
2. **El Picador de Ryder pasa a manejarlo un invitado.** Al entrar la etapa de atrapar
   (`L(89) >= 10`), el dueño frena la grabación del auto (`STOP_PLAYBACK_RECORDED_CAR(108@)`) una sola
   vez por intento. El auto queda quieto, con Ryder arriba (sigue unido), y el invitado lo levanta y lo
   maneja él (el sync de vehículo ya es por conductor: `VEHICLE_DRIVER_UPDATE`).
3. **Un auto igual para el resto.** El mismo módulo, en esa misma etapa, llama
   `ParkSharedGuestCar(car, script, "catalyst", perVehicle = otros invitados)` para que aparezca
   **un solo** Picador 600 extra al lado (con blip), donde suben los demás invitados. Si hay un solo
   invitado no se crea ninguno (`perVehicle < 1`).
4. **Lo que NO cambia:** el conteo lo decide siempre Ryder en el juego del dueño; las cajas las crea
   `HostThrow` en el juego del dueño y bajan por `CObjectSync` (1 id por objeto). La colisíón de las
   copias sigue su curso aparte (ítem abierto `object-moving-nocollide`).

## Archivos
- `client/src/CCatalystRyderCar.h/.cpp` (nuevo): módulo del dueño, etapa 10, freno de grabación + auto extra.
- `client/src/CMissionSyncRules.cpp`: llamada a `ProcessCatalystRyderCar()` junto a
  `ProcessCatalystGuestCars()` (`:186`).
- `client/src/CMissionGuestActions.cpp`: RYDER3 sin sesión cuando el interruptor está encendido.
- `client/src/CConfigLoader.h/.cpp`: interruptor nuevo (mapa de defaults, lectura, escritura).
- `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `docs/MISIONES_HECHAS.md`: registro.

## Aceptación (para probar en juego)
- [ ] Catalyst con 1 invitado: el invitado maneja el Picador de Ryder; Ryder va arriba y atrapa las
      cajas que el host lanza desde el vagón; el contador sube igual que en individual.
- [ ] Con 2-3 invitados: aparece **un** Picador 600 extra al lado del de Ryder; los demás suben ahí;
      no aparece un auto por invitado.
- [ ] El invitado no puede lanzar cajas (mantener disparo no muestra la barra de potencia ni suma).
- [ ] El invitado no queda enganchado al vagón del tren.
- [ ] Interruptor apagado: comportamiento de hoy (invitados arriba del vagón lanzando).

## Riesgos / bordes
- La grabación 486 deja de mover el auto: si nadie lo levanta, Ryder queda quieto y la misión se
  queda sin atrapar cajas hasta que alguien lo maneje. Es el comportamiento pedido, pero hay que
  avisarlo en juego (texto de ayuda).
- El auto de misión creado por el script del dueño pasa a ser conducido por un invitado: si el sync
  de vehículo no acepta el traspaso de conductor en un auto MISSION del dueño, hay que reasignarlo
  (queda anotado como punto a verificar en el primer test).
- `Car.IsPlaybackGoingOn(108@)` (`:1547`) se consulta antes de la etapa 10; frenar después no rompe
  ese `UnpausePlayback`.
