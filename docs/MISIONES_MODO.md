# MISIONES_MODO — qué modos permite cada misión

> Decisiones de juego en [REQUISITOS.md](REQUISITOS.md). IDs según `DEFINE MISSION` en `scm/main.txt`. Para adaptaciones por misión, ver [MISIONES_HECHAS.md](MISIONES_HECHAS.md). Resultados de juego en [TESTEAR_HOY.md](../TESTEAR_HOY.md).

## Reglas generales
- **Historia** (ids no secundarios): modo individual o coop. Bloqueo por id: nadie más inicia la misma hasta que termine/falle/se vaya el dueño (`CMissionLock`).
- **Secundarias** (ids 3–9, 35, 71, 83, 113–134): **no se bloquean**; cada jugador puede hacer la misma a la vez en individual. Al iniciar en coop, la juegan los que aceptan.
- **Coop**: si un participante muere o lo arrestan falla para todos; si abandona (mantener `2`) se va solo él; si el dueño cancela, termina para todos y se avisa en el chat.
- **Individual**: solo afecta a quien la juega; el resto no ve textos ni "misión iniciada/terminada".
- Valor por defecto de una misión nueva: individual y coop.

## Tabla de modos
| Misión | Script (id) | Modos permitidos | Notas |
|---|---|---|---|
| Paramédico | AMBULAN (122) | coop e individual | el paciente sube a la ambulancia de cualquier jugador de la misión |
| Vigilante | COPCAR (124) | coop e individual | |
| Taxi | TAXIODD (121) | **solo individual** | |
| Chulo (Proxeneta) | PIMP (127) | **solo individual** | |
| Autoescuela de coches | DSKOOL (71) | **solo individual** | 360, 180, derrape y parada, pinchazo y control, vuelta rápida, esquiva los conos, el 90, a dos ruedas, gira y sigue, técnica de detención, alley oop, paseo por la ciudad |
| Autoescuela de motos | BSKOOL (120) | **solo individual** | |
| Autoescuela náutica | BOAT (119) | **solo individual** | |
| Autoescuela de aviación | DESERT5 (83) | **solo individual** | despegue, aterrizaje, vuelta a la pista, destruir objetivos, rizo, túnel, paracaídas |
| Carreras con lobby VS | CPRACE (35) | individual, VS solo jugadores o VS con NPC | El que inicia elige el modo. En VS participan los jugadores que aceptan la invitación (`CVsLobby`, `CVsRace`). |
| Air Raid | ZERO1 | coop e individual | coop-mission-machines=true, ametralladora propia por invitado junto a la del dueño; los derribos del invitado cuentan en el dueño (InflictDamage). Apagado: espectador como hoy |
| Supply Lines... | ZERO2 | coop e individual | RC Baron propio por invitado en el mismo techo; el daño a las metas compartidas cuenta en el dueño. Apagado: espectador como hoy |
| Robbing Uncle Sam | RYDER2 | coop e individual | coop-mission-machines=true, carretilla propia por invitado; las cajas que mueve cuentan en el dueño por el canal de objeto movido (GUEST_OBJECT_MOVE). Las cajas de botín a tiros siguen sin contar para invitados |
| New Model Army | ZERO4 | coop e individual | coop-mission-machines=true, RC Goblin con imán propio por invitado sobre la mesa (interior area 10, `CMissionWarTable`); los barriles y tablas que lleva mueven el objeto real del dueño (GUEST_OBJECT_MOVE + `released`) y la misión cuenta sola. La bomba sigue del dueño |
| Up, Up and Away! | HEIST5 | coop e individual | la minigun del depósito da puesto propio a cada invitado (como ZERO1, mismo TURRET); los Hunter derribados cuentan en el dueño. El Leviathan con el furgón sigue del dueño (un solo helicóptero, `Char.IsInCar($scplayer, 34@)`) |
| Customs Fast Track | STEAL4 | coop e individual | Fuera: la grúa es local de cada juego (tanda 2026-11-12, "D: resultado") |
| Cop Wheels | HEIST4 | coop e individual | el invitado que lleva una moto de misión a la rampa del Packer la carga y cuenta en el dueño (CMissionGuestCounts + attach replicado); depende de que el invitado pueda subirse a la moto |
| Cesar Vialpando | SWEET6 (19) | **coop (puntaje en equipo) e individual** | coop-lowrider=true, concurso local de cada invitado y suma al dueño; apagado conserva individual y CMissionLock |
| Life's a Beach | MUSIC1 | coop (baile en equipo) e individual | coop-dance=true, DANCE local por invitado y suma al dueño; clubes intactos |
| Resto de la historia | — | coop e individual | revisar caso por caso las que no se puedan en coop |
