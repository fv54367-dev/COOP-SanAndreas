# Tarea T26_bala_remota_normal: la bala remota no debe usar un punto de colisión sin inicializar
Leé `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. Sin interruptor: es la corrección de un valor inválido.

## Problema (probado en juego)
Al disparar desde un auto, en ciertos ángulos o con ciertas armas, un auto sale volando o da vueltas.
Causa probable, en `client/src/PacketHandlers/players.cpp`, handler `PLAYER_BULLET_SHOT`: `CColPoint colPoint` se llena con `CWorld::ProcessLineOfSight`, pero si en esta PC el rayo no choca (las posiciones difieren entre clientes) la estructura queda sin escribir. Después `victim` se toma del paquete, y `CWeapon::DoBulletImpact` hace `victimVeh->ApplyForce(hitCP.m_vecNormal * fuerza, hitCP.m_vecPoint - victimVeh->GetPosition(), true)` (gta-reversed `Weapon.cpp:617`) con una normal basura. Verificá primero si el constructor de `CColPoint` del SDK inicializa algo.

## Cambio
En ese handler:
1. Dejá `colPoint` en cero antes de usarlo.
2. Guardá el resultado de `ProcessLineOfSight`. Si no chocó, usá como normal la dirección opuesta al disparo (`-(endPos - startPos)` normalizada) y piezas y superficies en 0 (salvo `m_nSurfaceTypeB` que ya copia el paquete para peds).
3. Aunque haya chocado, si la normal no es finita o su largo no está cerca de 1, reemplazala por la misma dirección opuesta.
4. Si `victim` es un vehículo y `endPos` queda a más de 15 m de su posición en esta PC, usá la posición del vehículo como punto de impacto, para no generar un torque enorme.

Revisá si otro camino llama a `DoBulletImpact` con un `CColPoint` armado a mano (grep en `client/src`), y aplicá lo mismo si tiene el mismo problema.

## Aceptación
Compilan client y server. CRLF. Diff acotado. Una línea en `TESTEAR_HOY.md` y en "Pendiente" de `ESTADO.md`. No instales ni toques los GTA. Resumen ≤6 líneas.
