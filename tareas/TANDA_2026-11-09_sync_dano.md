# Tanda 2026-11-09 sync de daño: una sola autoridad por NPC

Pedido del dueño: que lo que ven los jugadores entre sí esté al día. Dos síntomas:
1. El invitado mata a un NPC, se vuelve a parar y queda quieto ("muerto pero quieto"), y de cerca le pega.
2. El host ve a un balla dispararle al invitado; el invitado ve luces y trazas, y nunca recibe los tiros.

## Causa (leída en el código, sin juego)
- **Daño repartido entre dos PC.** El que dispara aplica el daño en su copia; el dueño del NPC solo se entera por la repetición del balazo (`PLAYER_BULLET_SHOT` -> `DoBulletImpact`, `PacketHandlers/players.cpp`). Golpes, atropellos y fuego no viajan. Si el dueño no registra lo mismo, su `PED_ONFOOT` le devuelve la vida a una copia que ya cayó (`peds.cpp`, `pPed->m_fHealth = ...`).
- **Los tiros de los NPC se vuelven a tirar en cada PC.** `PED_SHOT_SYNC` llama `weapon.Fire` en la copia, apuntando a donde estaba el jugador en la PC del dueño; si `Fire` falla solo dibuja la traza (`CBulletTraces::AddTrace`). El daño al jugador lo decide ese segundo tiro, no el dueño del NPC.

## T46: `ped-damage-authority` (Claude a mano: hooks y protocolo)
Regla: el **dueño (syncer) del NPC decide su vida y su muerte, y decide si sus NPC le pegaron a un jugador**.
- Copia de un NPC: solo acepta daño del jugador local (balas, golpes, atropello). Ese daño se toma del cálculo del juego, por evento, y se manda al dueño: `PED_DAMAGE`. El dueño lo aplica con `CWeapon::GenerateDamageEvent` (0x73A530, gta-reversed `Weapon.cpp:158`) con el jugador remoto de autor, y ajusta al valor exacto.
- La copia no se cura durante 1,5 s tras un daño propio. Copia muerta con dueño que la da por viva más de 1 s: se rearma (`StreamOut`/`StreamIn`), log `ped-copy-revive`.
- La repetición del balazo ajeno ya no daña NPC (ni en el dueño ni en terceros): queda para trazas, vehículos y jugadores. La sangre se agrega a mano.
- NPC propio que daña a otro jugador (bala o golpe): se mide en la copia del jugador y se manda `PED_HIT_PLAYER` solo a ese jugador, que se lo aplica. El jugador ignora el daño de armas que venga de copias ajenas.
- Gancho: los 25 sitios de `CEventDamage::AffectsPed` ya enganchados en `DamageHooks.cpp` (gta-reversed `EventDamage.cpp:78`).

Archivos: `shared/config.h` (0.4.21-alpha), `shared/network/packet_types.h`, `shared/network/packets/peds.h`, `client/src/CPedDamageSync.{h,cpp}` (nuevo), `client/src/Hooks/DamageHooks.cpp`, `client/src/CNetworkPed.{h,cpp}`, `client/src/CNetworkPedManager.cpp`, `client/src/PacketHandlers/{peds,players}.cpp`, `client/src/CConfigLoader.{h,cpp}`, `client/src/Main.cpp`, `server/src/PacketHandlers/peds.cpp`, `INTERRUPTORES.md`, `TESTEAR_HOY.md`, `ESTADO.md`.

**Protocolo: sube a 0.4.21-alpha** (dos paquetes al final de `packet_types.h`). Dueño del protocolo en esta tanda: Claude. Quien tenga ramas abiertas, rebasea.

Casos borde:
- 3-4 jugadores: los terceros descartan el daño repetido en sus copias; solo aplica el dueño. Lo ven por la vida sincronizada.
- Uno en individual (F1): igual que hoy con las balas; el server no filtra por misión.
- El dueño cambia o se desconecta con el paquete en viaje: el server lo manda al dueño actual; si quien lo recibe no lo sincroniza, lo descarta. La copia se rearma sola.
- Aliado protegido, invulnerable o cinemática: el juego del dueño rechaza el daño (`AffectsPed` nativo y `rule-ally-protected`); la copia del que disparó se rearma.
- NPC con más de 255 de vida: la vida viaja en un byte; el dueño resta lo medido y no lo mata de un tiro aunque la copia haya muerto.
- Explosiones, molotov y lanzallamas: no se reportan; ya viajan por su lado y el dueño los simula.
- Copia del NPC fuera de rango en el jugador herido: el daño se aplica sin autor.
- Interruptor `ped-damage-authority = false`: todo como antes. Tiene que valer lo mismo en todos los clientes.

Aceptación: compila client, server y testbot; testbot TODO PASS. En juego (dueño): pruebas en `TESTEAR_HOY.md`.

## Estado (2026-11-09)
Commits `dc020ab` (primera versión) y el siguiente (correcciones de la revisión). Client, server y testbot compilan; testbot TODO PASS. **SIN PROBAR en juego.**

Revisión cruzada: Codex local (solo lectura, sobre `dc020ab`) y Codex del amigo (crítica de diseño por la Red de IAs). Aplicado:
- **Crítico, los dos lo encontraron:** `CPedDamageResponseCalculator__ComputeWillKillPed_Hook` (`PlayerHooks.cpp`, 0x4B5B27) ya devolvía la vida a las copias y ponía el daño en cero: la medición por diferencia de vida daba cero. Ahora el daño se toma ahí mismo, por evento (`CPedDamageSync::OnDamageComputed`: `m_fDamageHealth + m_fDamageArmor`, arma y pieza del calculador, gta-reversed `PedDamageResponseCalculator.h`). Se fueron la medición por frame, `Flush` y el campo de dirección de los paquetes. Con esto también se cierran: pérdidas superpuestas con varios NPC, chaleco pisado antes de medir y streaming antes del envío.
- **Golpe doble:** el ped del jugador remoto también pega y atropella en la PC del dueño. El dueño descarta ese daño directo (llega medido por `PED_DAMAGE`).
- **Aliados:** `PED_DAMAGE` sin autor presente se descarta (la protección de aliados necesita la fuente).

Anotado, sin hacer:
- `killed` mata al NPC del dueño si tiene 255 o menos de vida, aunque la copia tuviera un valor atrasado.
- Ocupantes de vehículo: una copia muerta dentro de un auto que el dueño da por viva no se rearma (`ApplyRemoteDeath` tampoco los cubre).
- El interruptor no se negocia al conectar: distinto en dos clientes pierde o duplica daño.
- El rearmado crea un ped nuevo (otro handle del pool); los blips se reponen solos, otras referencias no.

## Primera prueba en juego (2026-11-10) y corrección
Resultado del dueño: atropello y herir sin matar OK; al invitado le llegan los tiros de los NPC. Fallaron: el host veía caminar un rato al NPC que mató el invitado; los golpes no producían reacción; el NPC que mata el host quedaba duro en el invitado; la copia que dispara no muestra la animación.
- **Causa (log `ped-damage-apply ... after=0 alive=1`):** `AffectsPed` vuelve a correr cuando el ped atiende el evento (reacción, muerte), ya fuera de la aplicación autorizada, y `ShouldDiscard` lo descartaba ahí. Ahora un evento con el daño ya calculado (`m_bDamageCalculated`, 0x42) no se descarta.
- **Muerte de la copia:** el `CTaskComplexDie` puesto a mano en el slot primario no la tiraba (mismo [N] del 2026-10-25); antes la tapaba la repetición del balazo. Ahora la copia muere con un evento de daño del juego (`CPedDamageSync::KillCopy`, log `ped-copy-kill`); si a los 500 ms sigue viva, queda la tarea a mano.
- Pendiente: animación de apuntar y disparar de la copia (punto 5 de `TESTEAR_HOY.md`).

## Segunda prueba (2026-11-10): confirmado
El dueño confirmó tiros, golpes, atropello, muerte desde el host y golpes de NPC al invitado. Quedan en `ESTADO.md` (bug 7): animación de apuntar/disparar de la copia y copias que tardan 1-2 s en caer.
