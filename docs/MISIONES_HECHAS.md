# MISIONES_HECHAS — adaptaciones por misión

Resumen del comportamiento cooperativo por misión. Para reportar o confirmar una prueba, usá [TESTEAR_HOY.md](../TESTEAR_HOY.md). Los modos disponibles están en [MISIONES_MODO.md](MISIONES_MODO.md).

Lo común a todas: el marcador espera a todos, todos ven las cinemáticas/textos/íconos del host, las bajas y el daño del invitado cuentan, si alguien muere o lo arrestan falla para todos, y cada uno guarda su progreso.
Lo que el invitado **no** tiene en ninguna: los enemigos persiguen al host (pidió apagarlo, interruptor `mission-hunters`).

## Los Santos

| # | Misión | Script | Adaptación para coop |
|---|---|---|---|
| 1 | Intro | INTRO | Cada uno ve su propia intro (`intro-independent`). |
| 2 | Big Smoke | INTRO1 | Reglas generales para la misión de introducción. |
| 3 | Ryder | INTRO2 | Esperas en pizzería y barbería. En las escenas con barras, si el invitado ya está cerca del host, conserva su posición. |
| 4 | Tagging Up Turf | SWEET1 | Los graffitis y la pintura del invitado cuentan. Las escenas de los graffitis avanzan al llegar el host y el invitado sigue pintando desde su posición. |
| 5 | Cleaning the Hood | SWEET1B | Reglas generales de historia. |
| 6 | Drive-Thru | SWEET3 | Auto compartido para invitados y segundo Greenwood después de la escena del Cluckin' Bell. |
| 7 | Nines and AK's | SWEET2 | Las botellas que rompe el invitado cuentan. |
| 8 | Drive-By | SWEET4 | Auto compartido para invitados y bajas de los cuatro grupos. Las armas siguen lo que recibe el host del script. |
| 9 | Sweet's Girl | HOODS5 | Reglas generales de historia. El auto extra genérico depende de `mission-no-midcar` y `mission-boarding-shared-car`. |
| 10 | Cesar Vialpando | SWEET6 | Concurso de lowrider en equipo; el host entra primero al garaje y el invitado recibe aviso de su lowrider. |
| 11 | Los Sepulcros | SWEET7 | Autos de invitados, cementerio, huida; auto de vuelta si perdieron el suyo y el reloj se frena mientras se espera. |
| 12 | Doberman | CRASH4 | Guerra de pandillas sincronizada. Las bajas de invitados cuentan. La guerra arranca con el host en el territorio. |
| 13 | Burning Desire | CRASH1 | Fuego sincronizado; el extintor/molotov que agarra uno también le llega al otro (antes se podía trabar). |
| 14 | Gray Imports | DRUGS3 | El jefe no se escapa si el invitado lo sigue de cerca. Solo el host puede manejar los autos de la misión. |
| 15 | Home Invasion | GUNS1 | Cajas por portador, ruido compartido y entrada por la puerta de atrás. |
| 16 | Catalyst | RYDER3 | Picador extra para los invitados. Con `mission-catalyst-guest-car=false` y `mission-guest-train=true`, el invitado se engancha al tren y puede lanzar cajas. Con `mission-catalyst-ryder-car=true` (2026-11-20): al entrar la etapa de atrapar, la grabación del Picador de Ryder se frena y **lo maneja un invitado**, con **un** Picador extra al lado para el resto; **sólo el host lanza** cajas. |
| 17 | Robbing Uncle Sam | RYDER2 | Montacargas por jugador y Greenwood compartido. |
| 18 | OG Loc | TWAR7 | Motos y auto compartido para invitados; Freddy no se escapa y su velocidad sigue al que lo persigue más cerca. |
| 19 | Running Dog | SMOKE2 | Asientos del 466 y auto compartido; persecución. |
| 20 | Wrong Side of the Tracks | SMOKE3 | Vehículos compartidos en el tramo inicial y motos de a dos en la persecución. |
| 21 | Just Business | DRUGS1 | Auto compartido y motos de escolta para invitados. |
| 22 | Life's a Beach | MUSIC1 | Baile en equipo, llegada a la fiesta, furgoneta. |
| 23 | Madd Dogg's Rhymes | MUSIC2 | Bici para cada invitado en la huida. El libro lo puede agarrar el invitado. |
| 24 | Management Issues | MUSIC3 | Un invitado va adelante en el Elegant y el resto en un auto compartido; en el muelle el invitado se baja solo. |
| 25 | House Party | MUSIC5 | Las bajas del invitado cuentan para las oleadas, el arma del pickup llega a todos y el reloj se frena en las esperas. |
| 26 | High Stakes Lowrider | CESAR1 | Carrera cooperativa con tablero de posiciones, grilla y validación de progreso de César. |
| 27 | Reuniting the Families | DRUGS4 | Los invitados van de artilleros en la persecución. El auto extra genérico depende de `mission-no-midcar`. |
| 28 | The Green Sabre | LA1FIN2 | Autos de invitados hasta Sweet. Al terminar, el invitado pasa a Badlands con su lanzador. |

Para secundarias, ver [MISIONES_MODO.md](MISIONES_MODO.md).

## Badlands en adelante (resumen)
| # | Misión | Script | Adaptación para coop |
|---|---|---|---|
| 29 | Badlands | BCRASH1 | Lanzador y cámara para invitados. El testigo puede ser visto o perseguido por cualquiera, y la foto y el daño al auto del invitado cuentan. |
| 30 | First Date | CATALIN | Lanzador de Catalina para invitados (nuevo; antes el progreso de Catalina se leía mal). Catalina vuelve al grupo solo con el host cerca. |
| 31 | Local Liquor Store | CAT1 | Un quad cada 2 invitados cuando el host agarra el suyo. |
| 32 | Small Town Bank | CAT2 | Motos de escape cada 2 invitados junto a la del host. Para intimidar a los rehenes el host tiene que tener un arma en la mano. |
| 33 | Tanker Commander | CAT3 | Auto compartido para invitados junto al camión (Catalina va en el otro asiento). |
| 34 | Against All Odds | CAT4 | Reglas generales. El script original usa los satchels del host para la caja fuerte y las estrellas son individuales. |
| 35 | King in Exile | CATCUT | Solo escena; el invitado recibe la marca de "ya vista" para que le vuelva a aparecer Catalina. |
| 36 | Body Harvest | TRUTH1 | Lanzador de The Truth para invitados, auto compartido junto a la cosechadora, y la cosechadora la puede manejar el invitado. |
| 37 | Are You Going To San Fierro? | TRUTH2 | Las plantas que quema el invitado cuentan y aparece un auto compartido junto al camper. |
| 38 | Wu Zi Mu / Farewell, My Love | BCESAR4 + carrera | Carreras del campo. El invitado observa la carrera y carece de lanzador propio para esta cadena. |

Desde San Fierro en adelante todavía no hay lanzador para invitados: hoy solo el host puede empezar esas misiones.
También hay adaptaciones para las fotos de Photo Opportunity y Architectural Espionage, las máquinas de Zero, las acciones de Dam and Blast y Amphibious Assault, los vehículos de Up Up and Away y Cop Wheels, y las acciones de Black Project, Green Goo y Vertical Bird.
Customs Fast Track: la grúa no se comparte todavía.
