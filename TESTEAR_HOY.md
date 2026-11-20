# TESTEAR_HOY — qué probar (actualizado 2026-11-19)

**Cómo marcar:** cambiá la cajita de cada línea: `[x]` anda bien · `[N]` no anda · `[I]` anda pero con algo raro · `[ ]` sin probar. Al lado de `[N]`/`[I]` escribí qué pasó (ej. `- [N] El invitado queda afuera en el vacío`).
**Dejá los `[x]` hasta que la IA registre tu confirmación en Git en la rama donde seguís.** Si vuelve a fallar en otro build, anotá cuál y qué pasó.

Para probar en esta PC, abrí `PROBAR_SOLO.bat`. El GTA de la izquierda es el host y el de la derecha es el invitado. Para abrir un tercer GTA, usá `JUGADORES=3` en el .bat. Si algo falla, guardá `CoopAndreas_mirror.log` de los dos GTA (junto a `gta_sa.exe`) antes de actualizar.
"Interruptor" = nombre que se pone en `false` en `coopandreas.ini` para volver a como era antes (lista en `INTERRUPTORES.md`). Todo con scripts originales; las pruebas automáticas (`tools/prueba_auto.py`) solo si las pedís.

**Orden:** 1) Misiones de historia, una por una en el orden del juego · 2) Cosas que valen para todas las misiones · 3) Secundarias y minijuegos · 4) Mundo y jugar libre. Lo más nuevo de cada misión va primero y lleva *(nuevo)*.

# 1. MISIONES DE HISTORIA

## Big Smoke
- [x] Salida de casa (cinemática): el CJ invitado sube a un asiento de pasajero libre, casi a la vez que el CJ original, sin quedar al lado del auto. (confirmado por el usuario 19-11, build con `intro-board`; ver ESTADO > Big Smoke). PENDIENTE aparte: con 3-4 jugadores los sobrantes todavía se ven de pie (falta ocultarlos).
- [x] (`intro-follow-host`) Al terminar, al invitado se le va el ícono de CJ y el marcador rojo sobre CJ (y le aparece el de Ryder).

## Ryder
- [x] *(nuevo 18-11)* Pizzería Well Stacked: el invitado compra y TODOS ven al pizzero hacer la secuencia de atender (bandeja incluida); el invitado ve la pizza de los comensales y la bandeja/caja del pizzero (protocolo 0.4.60, `interior-object-sync` + `interior-service-guest`). Confirmado por el usuario: funciona (la animación del pizzero 'medio raro' pero funciona, marcada).
- [x] *(nuevo 18-11)* Pizzería: NO debe verse un pizzero duplicado en la misma caja al entrar los dos (arreglo `76600db`, confirmado por el usuario 18-11): si el dueño del interior parpadea, el keeper nativo se adopta en el record de traspaso por menor distancia y se despublica el duplicado (`interior-dedupe-service` / `interior-adopt-service-visible` en el mirror.log). Los dos empleados en cajas distintas son vanilla (Int_Manager lanza junkfud + FoodBrains).
- [x] *(nuevo 18-11)* Dentro de la pizzería Well Stacked (y el barber) el invitado ya cerca del host NO se teletransporta durante las escenas con barras (antes: 6 warps de 1-4 m en el asalto); se queda donde está mirando la escena, a lo sumo desaparece de su pantalla si tapa la cámara. Si se mueve: `scene-guest-place` en el `mirror.log` del invitado.
- [x] Mientras el host juega Ryder, al invitado no se le borra el marcador de Ryder después (al terminar o fallar sigue/queda bien).
- [x] Invitado que carga un guardado más adelantado (ej. después de Cleaning the Hood): no aparece la misión Ryder en el mapa.

## Tagging Up Turf
- [x] *(nuevo 18-11 tarde)* La misión del host NO espera a nadie: los diálogos/escenas de los graffitis arrancan cuando llega el host aunque el invitado esté pintando lejos (ya no sale "Esperando a Jugador2"). Si espera igual: `rule-wait-scene` en el `mirror.log` del host.
- [x] *(nuevo 18-11 tarde)* Al invitado NO lo teletransportan a las escenas: sigue pintando donde estaba y la escena le llega igual (antes lo subían a los techos desde 46-71 m). Log: `scene-guest-keep reason=sweet1-free` en el invitado.
- [x] *(nuevo 18-11 tarde)* Invitado manejando el Greenwood: al bajarse en "vos marcá por acá", Sweet arranca con el auto (antes quedaba quieto para siempre). Server: `[vehicle] mission car`, invitado: `exit-packet-scripted`.
- [x] El graffiti que pintó el invitado NO se despinta cuando al host le llega la escena de los Ballas (no hay que repintar).
- [x] (`mission-scene-skip-no-control`) Pintar el último graffiti con los dos lejos: la escena de Sweet arranca sola, nadie queda congelado.

## Drive-Thru
- [x] Después de la cinemática del Cluckin' Bell aparece un Greenwood detrás del del host con los invitados adentro (uno maneja, el resto de acompañantes); el de antes desaparece. (confirmado por el usuario 19-11)
- [x] Entrando en auto a la cinemática, el auto del invitado queda detrás; a pie, escondido. Sin autos duplicados al final. (confirmado por el usuario 19-11)
- [x] Al llegar al último marcador nadie es bajado del auto (si pasa, `mirror.log` del invitado: buscar `scene-guest-exit-vehicle` o `rx-scripted-exit`). (confirmado por el usuario 19-11)

## Drive-By (SWEET4)
- [x] El auto extra queda estacionado en un lugar accesible y los invitados suben por su cuenta. Quien ya tiene vehículo lo conserva. (confirmado por el usuario 19-11)
- [x] Un solo auto extra para todos los invitados, con ícono, al lado del Greenwood. (confirmado por el usuario 19-11)

## Cesar Vialpando
- [x] Con Lyrax de host: después de actualizar, cargar su partida; en Grove Street aparece el marcador de Sweet y la misión arranca en coop con los dos en el círculo. Guardar y volver a cargar: el marcador sigue estando. (confirmado por el usuario 19-11)
- [x] El invitado ve el subtítulo de César y el panel "Apuesta". (confirmado por el usuario 19-11)
- [x] Cuando le dan el lowrider al host, el invitado recibe uno igual al lado; sale "Subi a tu lowrider (icono azul)". (confirmado por el usuario 19-11)
- [x] En el garaje Loco Low Co. entra uno a la vez: con un invitado adentro, al host le sale "Garaje ocupado. Espera afuera tu turno" y no entra. (confirmado por el usuario 19-11)
- [x] El host llega al garaje y entra primero aunque haya un invitado al lado. (confirmado por el usuario 19-11)
- [x] El host pone pausa (Esc) unos 5 s durante la preparación o el concurso: el lowrider del invitado no desaparece. (confirmado por el usuario 19-11)
- [x] En los saltos cada uno con su auto y chica; los dos suman contra el rival. (confirmado por el usuario 19-11)
- [x] Al terminar, el invitado conserva su lowrider. (confirmado por el usuario 19-11)

## Los Sepulcros (SWEET7)
- [ ] En la salida aparece un solo Greenwood extra, visible y con ícono, para los invitados a pie.
- [ ] Cementerio: todos ven texto, reloj e íconos; los invitados pueden matar guardias y a Kane y cuenta una vez.
- [ ] Mientras el host espera a alguien en un marcador, el reloj se frena y después sigue.
- [ ] La vuelta a Grove espera al grupo.
- [ ] Si los invitados perdieron el Greenwood y quedaron a pie, cuando el host sale del cementerio con el Admiral aparece un auto compartido para ellos.
- [ ] Al saltar la escena, fallar, morir o desconectarse no quedan autos ni copias congeladas.

## Burning Desire
- [ ] (`mission-pickup-weapon-share`) El invitado agarra el extintor primero: al host también le aparece y la misión sigue.
- [ ] El host agarra el extintor: el invitado también lo recibe y puede apagar fuegos. Lo mismo con el molotov del principio.
- [ ] Al terminar, el invitado no recibe armas del piso que nadie agarró.

## Gray Imports
- [ ] (`coop-gray-imports`) El host se queda atrás y el invitado sigue al jefe de cerca: no se escapa.

## Catalyst (RYDER3)
- [ ] Con `mission-catalyst-guest-car=true`, aparece un Picador extra con ícono para los invitados. Uno lo maneja y otro puede acompañarlo. El contador solo sube con las cajas que atrapa Ryder.
- [ ] Con `mission-catalyst-guest-car=false` y `mission-guest-train=true`, el invitado queda arriba del vagón trasero. Al terminar el lanzamiento se despega. Usá esta configuración también para probar el ping y el lanzamiento desde el vagón.
- [ ] Con ~100 de ping: el invitado arriba del vagón trasero va fluido.
- [ ] Lanzar cajas: mantener disparo para cargar y soltar mirando a Ryder; cuenta igual que las del host. Dos invitados pueden lanzar; fuera del tren no deja.
- [ ] (`object-moving-nocollide`) Las cajas que caen en la camioneta en marcha y las del tren se mueven sin chocar ni empujar al invitado.
- [x] Primera cinemática: con el invitado a unos metros, el host no arranca hasta que llega ("Esperando a X"). A los 90 s sigue sola.
- [x] Mini cinemáticas (barras negras): esperan al que quedó atrás o en otro interior.
- [x] Después de la mini cinemática de los Ballas, el invitado cerca y a la vista NO se teletransporta.

## Robbing Uncle Sam
- [ ] Al salir de Grove hay un Greenwood para los invitados al lado de la Mule.
- [ ] (`coop-robbing-uncle-sam`) El invitado recibe SU montacargas al lado del del host; cada jugador de la misión (hasta 4) tiene el suyo, aunque siga dentro del auto al llegar.
- [ ] Al acercar las paletas a una caja, se mueve (no es una pared) y al dejarla en el carrito cuenta.
- [ ] (`coop-robbing-guest-deliver`) El invitado lleva una caja con su montacargas al fondo de la Mule: la caja se va y el contador CAJAS sube (una sola vez).
- [ ] Cuando el host entrega una caja, los invitados no se teletransportan ni les sale una caja propia (si sale, anotar cuándo y dónde).

## OG Loc
- [ ] Al sacarlo de la cárcel el auto de invitados aparece con los invitados adentro (uno maneja).
- [ ] (`coop-og-loc-escape`) El host lejos y el invitado sigue a Freddy: no se escapa.
- [ ] Freddy acelera o frena según el que lo sigue más cerca (host o invitado).
- [ ] Saltear viaje: fracasar después del 2.º marcador, reintentar y aceptar el salto con D-pad derecha: el invitado se teletransporta al host y entra a un asiento libre del auto o a un auto que aparece al lado.

## Running Dog (SMOKE2)
- [ ] Con 2: cuando el host sube con Smoke, el invitado aparece sentado atrás (sin auto extra). Con 3-4: dos atrás y el que sobra en un auto propio.
- [ ] El auto extra del primer tramo queda estacionado con ícono para el que no entra con Smoke. El marcador de llegada espera al último a 50 m.
- [ ] Si el invitado se pone al volante sale "Solo <host> puede manejar este vehiculo en esta mision".
- [ ] Persecución: si el host queda lejos pero el invitado sigue al fugitivo, no se pierde.
- [ ] El invitado mata al fugitivo: la misión se gana.

## Wrong Side of the Tracks
- [ ] Con 2: cuando el host sube con Smoke, el invitado aparece sentado atrás. Con 3-4: dos atrás y el que sobra en un auto propio.
- [ ] Tramo de la moto: de a 2 por moto.
- [ ] En la persecución, si el invitado se pone al volante sale "Solo <host> puede manejar este vehiculo en esta mision".
- [ ] Cuando el tren se escapa, la escena final arranca sin esperar a los demás.

## Just Business
- [ ] Con 2: cuando el host sube con Smoke, el invitado aparece sentado atrás. Con 3-4: dos atrás y el que sobra en un auto propio.
- [ ] El invitado maneja el auto con Smoke y el host va a pie o en otro auto: la misión avanza.
- [ ] Las motos con Grove de los invitados salen apenas detrás de la de Smoke (0,5 s, 1 s, 1,5 s).

## Life's a Beach (MUSIC1)
- [ ] Host llega a la fiesta e invitado lejos: el host no ve la escena hasta que el invitado está a ~60 m; después arranca para los dos sin teletransportar.
- [ ] El invitado cerca escucha la música de la playa; al terminar se va.
- [ ] Baile con 2-4: cada uno en su lugar, con sus flechas, música y cámara; nadie atraviesa la furgoneta ni a los NPC.
- [ ] Arriba a la derecha sale "EQUIPO X / 5000"; se aprueba si entre todos llegan a la meta (3 jugadores: 7500, 4: 10000).
- [ ] Si el invitado abandona o se desconecta, no falla para los demás. Si muere, sí.
- [ ] Invitado lejos (>40 m): su pantalla nunca queda negra (vuelve en ~4 s).
- [ ] Después del baile el invitado sigue con SU ropa en la pantalla del host.
- [ ] Furgoneta al garaje: invitado maneja y host de acompañante: el host pasa al volante, entra sola y la escena termina.
- [ ] Invitado maneja solo (host afuera): ¿avanza la misión? Anotar qué pasa.

## Madd Dogg's Rhymes
- [ ] (`coop-madd-dogg`) Al salir con el libro, cada invitado tiene una bici con ícono al lado de la del host.

## Management Issues
- [ ] (`coop-management-issues`) Después de la escena, un invitado aparece adelante en el Elegant y los demás en un auto compartido.
- [ ] En el muelle, cuando el host se baja del Elegant, el invitado de adentro se baja solo y el auto cae al agua con la escena.

## High Stakes, Low-Rider
- [x] Durante la carrera se ve solo la tabla «POSICIONES» abajo a la derecha, también en el GTA del host. La cuenta regresiva sigue saliendo. (confirmado por el usuario 19-11)
- [x] El título "POSICIONES" se lee bien (letra tipo GTA, no la gótica) y a su derecha hay un reloj que cuenta desde la largada y se congela (celeste) cuando llegás.
- [x] Invitados que ya estaban en un auto (manejando o de acompañante) siguen adentro después de la cinemática de César (si no: `cutscene-keep-seat` en el `mirror.log` del invitado).
- [x] Un invitado maneja y el host va de acompañante: al largar el auto queda derecho en la grilla, mirando como los demás.

## Reuniting the Families
- [ ] Con `mission-no-midcar=true`, al subir el host al Greenwood de la misión, los invitados sin asiento siguen a pie. Si aparece un auto extra, anotá en qué tramo pasó.
- [ ] Persecución final: cuando el host está arriba del auto con la AK, el invitado también aparece enganchado y puede disparar; al terminar se suelta solo.
- [ ] Persecución final: el invitado enganchado al auto no se suelta solo a cada rato.

## The Green Sabre
- [ ] (`coop-green-sabre`) Al subir el host al Bravura, un invitado va de acompañante y los demás en un auto compartido.
- [ ] Al terminar, el invitado también queda en Badlands y le aparece el marcador de Badlands.

## Badlands (`coop-badlands`, `vehicle-damage-any-player`)
- [ ] Los dos tienen cámara al empezar la misión.
- [ ] Cuando el testigo escapa en el auto, el invitado le dispara o lo choca y el auto se rompe.
- [ ] Si el invitado sigue al testigo y el host queda lejos o sin verlo, la misión NO falla.
- [ ] Muerto el testigo, el invitado le saca la foto y la misión sigue (marcador para devolver la cámara).
- [ ] Si el host se aleja del cadáver mientras el invitado saca la foto, la misión NO falla.
- [ ] Después de Badlands, al invitado le aparece el marcador de Catalina y puede empezar sus misiones con el host.

## Misiones del campo (`coop-countryside`)
- [ ] Local Liquor Store: cuando el host sube al quad, aparece un quad para el invitado al lado.
- [ ] Small Town Bank: al salir del banco hay una moto para el invitado al lado de la del host.
- [ ] Tanker Commander: al lado del camión hay un auto para el invitado.
- [ ] King in Exile: al invitado le sigue apareciendo el marcador de Catalina después.
- [ ] Body Harvest: hay un auto para el invitado cerca de la cosechadora, y si el invitado la maneja la misión sigue igual.
- [ ] Are You Going To San Fierro?: las plantas que quema el invitado cuentan y hay un auto para él al lado del camper.

## Misiones de más adelante (`coop-mission-machines`, `mission-guest-actions`, `mission-guest-counts`, `coop-photos`)
- [ ] Fotos (Photo Opportunity, Architectural Espionage): las del invitado cuentan, una vez por objetivo.
- [ ] Air Raid (ZERO1): el invitado tiene su propia ametralladora; sus derribos cuentan.
- [ ] Supply Lines (ZERO2): el invitado tiene su avioncito RC en el mismo techo; sus tiros cuentan.
- [ ] New Model Army (ZERO4): el invitado recibe su helicóptero con imán y puede levantar barriles y tablas; cuentan.
- [ ] Amphibious Assault (WUZI2): el invitado usa el micrófono del barco y cambia el objetivo.
- [ ] Dam and Blast (HEIST2): el invitado pone una carga en su marca y el contador sube una vez.
- [ ] Cop Wheels (HEIST4): el invitado sube a una moto de la misión y la carga en la rampa: queda en el camión (también en su pantalla), el contador (uno solo) sube y el CJ del host no se mueve.
- [ ] Cop Wheels con 3-4: dos motos distintas cuentan las dos; invitado en F1 o muerto no cuenta; si se desconecta o muere con la moto sin cargar, la misión la vuelve a pedir.
- [ ] Up Up and Away (HEIST5): el invitado cerca recibe su minigun; los Hunter que derriba cuentan.
- [ ] Black Project: el invitado abre la compuerta desde el interruptor.
- [ ] Green Goo: el invitado agarra el frasco y el contador sube.
- [ ] Vertical Bird: si un guardia ve al invitado, suena la alarma.
- [ ] Puertas de misión (CAT4, DESERT9, HEIST2, CASINO6): se cierran también para el que sigue afuera.
- [ ] Misiones del casino, Madd Dogg's y demás: arrancan solo con todos en el marcador. Anotar cuáles andan.

# 2. EN TODAS LAS MISIONES
- [x] *(nuevo)* F1 → "Cinemáticas" = "Un solo CJ": en NINGUNA cinemática ves a los otros CJ, tampoco en las de Drive-By, Los Sepulcros, Running Dog, Wrong Side, Just Business ni en las escenas con barras dentro del juego. Con "Varios CJ", como siempre. (confirmado por el usuario 19-11)
- [ ] Lanzadores: el invitado ve el marcador que corresponde a su partida. Si tiene la misma misión pendiente que el host, puede entrar con él a Tenpenny (Burning Desire), César (High Stakes) y Reuniting the Families.
- [ ] Con `story-help-ask=false`, si el host y el invitado llegan al lanzador de High Stakes con distinto progreso de César, sale «Progreso distinto... F1» y la misión no arranca.
- [ ] Saltear viaje en cualquier misión que lo ofrezca (el juego muestra "SKIP_1" / D-pad derecha): el invitado se teletransporta al host y entra a un asiento libre o a un auto al lado.
- [ ] Íconos de misión (ej. Burning Desire): el invitado ve los mismos que el host, sin repetidos; al terminar se van.
- [ ] Pickups de misión: el invitado los ve; si los agarra él, avanza la misión y desaparecen para todos.
- [x] (18-11) (`phone-call-keeps-onmission`) La llamada de Hernández llega al host y termina normalmente. En el intento anterior se cortaba y volvía a sonar.

# 3. SECUNDARIAS Y MINIJUEGOS

## Carreras: 8-Track y carreras VS
- [ ] Con 3-4: cada uno en su lugar de la parrilla, el puesto es correcto y la tabla final muestra a todos en orden de llegada.
- [x] Al entrar al marcador aparece el menú viejo (ventana arriba al centro, también con la pantalla en negro) con 1 = VS contra jugadores y NPC, 2 = VS solo contra jugadores, 3 = solo contra NPC; cada tecla hace eso. El invitado responde 1 = sí, 2 = no. La oferta sale también para el invitado y con 3-4 jugadores.
- [x] Si todos rechazan o nadie llega al marcador, la carrera no arranca (vuelve al lado del marcador) y al invitado que esperaba le pasa lo mismo.
- [x] VS con NPC (tecla 1): el host corre con sus NPC; el invitado ve esos mismos NPC y arranca detrás de la grilla, sobre la pista, no metido en una estructura (si falla: `vs-race-npc-behind` en el `mirror.log` de ese jugador).
- [x] VS con NPC: la tabla POSICIONES muestra también a los NPC ("Corredor N", en gris) en su orden real, en la pantalla de todos.
- [x] Solo se ve la tabla "POSICIONES" con letra tipo GTA y reloj a la derecha (se congela en celeste al llegar); sin el cuadrito "N/total tiempo" ni el cartel viejo de puesto/vueltas, tampoco en la pantalla del que crea la carrera (si sigue: `vs-race-native-hud-hidden` en su log). La pantalla de resultados del final sigue saliendo.

## Billar (`pool-two-players`, F7)
- [x] *(nuevo)* Arrancá el billar desde el GTA derecho y uní el izquierdo con F7. Repetí invirtiendo quién lo inicia. Cuando aparezca «Información» por primera vez, la ventana que recibe el HUD sigue abierta y muestra el panel. (confirmado por el usuario 19-11)
- [x] (18-11, `local-e453b50`; arreglado en `97b8ec5`) El crash del GTA 2 al recibir el primer panel «Información» (0x573F08) NO vuelve: el billar funciona completo (confirmado por el usuario 19-11).
- [x] *(nuevo)* En ambos GTA aparecen las mismas ayudas para colocar la blanca, apuntar y tirar. El aviso de Espacio sigue el turno y se borra al avanzar. (confirmado por el usuario 19-11)
- [x] *(nuevo)* Los dos ven la blanca y el punto donde pega el taco, en la misma esquina y moviéndose juntos. Al embocar una bola, ambos paneles muestran el número (por ejemplo, «Bola metida 9»), las lisas o rayadas y, si la hubo, la misma falta. (confirmado por el usuario 19-11)
- [x] (18-11, `local-43376c3`) Tras una falta, el invitado vio el aviso de Espacio y los grupos de bolas, y pudo avanzar y apuntar durante más de 5 s sin que se cortara la partida. Ambos jugaron varios turnos.
- [x] *(nuevo)* Salí con F7 desde el invitado mientras apunta. Los dos quedan de pie dentro del bar y recuperan el control. Si el host cae al vacío, guardá las líneas `pool-stop` y `pool-owner-restore`. (confirmado por el usuario 19-11)
- [x] *(nuevo)* Cuando cambia el turno, el invitado ve «TU TURNO» y el host «TURNO DEL RIVAL». Al invitado le llega la toma desde su lado de la mesa y ve a su CJ apuntar. (confirmado por el usuario 19-11)
- [x] *(nuevo)* En el turno del invitado, llevá el taco hacia atrás y empujalo hacia adelante. La postura de CJ y el movimiento del taco muestran la preparación. Probá un golpe suave y uno fuerte y mirá cómo salen las bolas. (confirmado por el usuario 19-11)
- [x] (18-11, `local-6a91bfd`) Al aceptar con F7, el billar por fin arranca y el host puede tirar.
- [x] (18-11, `local-6a91bfd`; tanda de turnos/HUD) El turno del invitado ya muestra aviso, toma clara y animación de cargar el taco: el billar funciona completo (confirmado por el usuario 19-11).
- [x] Al romper el triángulo se mueven las mismas bolas en las dos pantallas; siguen la tirada sin saltos grandes y terminan en la misma posición. (confirmado por el usuario 19-11)
- [x] En el turno del invitado, sus controles apuntan y tiran (también pulsaciones cortas seguidas); en el del dueño no mueve nada. F7 devuelve el control y la cámara a los dos. (confirmado por el usuario 19-11)
- [x] Si una bola no aparece o vuelve a desincronizarse: guardar las líneas `pool-view-tx/rx/invalid` y `pool-ball-view` del `mirror.log` de ambos. (confirmado por el usuario 19-11)

## Desafíos de a dos (U ofrecer, Y aceptar, N rechazar)
- [ ] Básquet, blackjack, ruleta, Kickstart, Blood Ring, triatlón.

## Paramédico, Vigilante, Bombero, Mensajero
- [ ] Paramédico coop: el dueño lejos del herido y el invitado llega con SU ambulancia: el herido sube (animación normal), cuenta como pasajero y al llegar al hospital el invitado cuenta la entrega.
- [ ] Vigilante en coop: el chaleco de premio también llega a los demás, una vez por misión.
- [ ] Secundarias en coop (Paramédico, Vigilante, Bombero, Mensajero BMX): el invitado juega; en el Mensajero ve los checkpoints.

# 4. MUNDO Y JUGAR LIBRE

## Animaciones de los otros jugadores — PENDIENTE, sin resolver (19-11)
- [N] El CJ del otro jugador se ve **duro / sin ninguna animación** mientras camina o trota, y también en las cinemáticas: el cuerpo se desplaza quieto, como un muñeco. Cada uno se ve bien a sí mismo y mal al otro, y pasa **a veces**. Se probaron tres arreglos en la rama `tanda/2026-11-19-anim-copias-main` (corregir el clip de locomoción, no arrastrar la posición cuando la escena controla la copia, y espejar animaciones durante la cinemática) y el síntoma siguió igual, así que quedó **sin integrar**: `main` se comporta como siempre. Diagnóstico completo en ESTADO > Animaciones de copias de jugador.

## Crashes
- [x] *(nuevo)* (`anim-destroy-notify`) Jugar libre un rato largo con los dos (autos, agacharse, peleas, entrar/salir de autos): no vuelve el crash del invitado al aparecer un auto (`0x81214A`, `RpMatFXMaterialGetEffects`). Si aparece `anim-destroy-notify` en el `mirror.log`, pasámelo aunque no crashee.

## Reclutas de Grove
- [x] El invitado (no el host) recluta un Grove apuntando y con el botón de reclutar; lo sigue; el host lo ve seguir al invitado; H lo despide.
- [x] Reclutar, G para que te siga y alejarse: sigue. H lo despide (se va el ícono).
- [x] Con 3-4, dos reclutando a la vez no se pisan.

## Vehículos
- [x] Barcos: el otro ve tu barco avanzar suave con estela, el timón y la hélice; barcos de NPC también; los anclados quedan quietos. (confirmado por el usuario 19-11)
- [ ] Trenes de tráfico (`train-sync-rail`): subte o tren de carga en una estación: el invitado lo ve arrancar y frenar a la vez que el host.

## Conexión y actualizar
- [ ] Conexión sin VPN: server con `upnp=true`, el otro entra pegando el código en `IP / code`.
- [x] Tu amigo actualiza con ACTUALIZAR.bat y abre el GTA: si sale un cartel de CoopSA pidiendo reiniciar, reinicia una vez y no vuelve a salir.

---
**Si crashea:** guardar el `.log` y el `.dmp` de `GTA_JUGADORx\CoopAndreas_crashes`. Si se congela 15 s queda un `hang_*.log`.
