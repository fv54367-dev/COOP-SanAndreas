# T92 - Drive-By / SWEET4 (17)

## Alcance y reserva
Solo CConfigLoader.cpp/.h, CMissionSyncRules.cpp, Commands/Commands/CCommandPutNetworkPlayersInCar.cpp, CScenePartner.cpp, CCutscenePartners.cpp (todos bajo client/src/), INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md, docs/MISIONES_MODO.md, docs/SISTEMAS.md y esta tanda. CRLF C++; sin git, SCM, protocolo, instalacion ni GTA. Resumen <=10 lineas.

## Critica previa del plan
La referencia SWEET4.txt contiene Coop.* y cambios del fork: no es el original binario. No usar sus offsets/locales como contrato del vanilla. Un auto lleno no admite invitados; no ampliar IS_CHAR_IN_CAR para que cualquiera conduzca por el dueno: romperia sus tareas y escenas. El Pay n Spray no admite cuatro autos juntos; el LOCATE de ayuda debe quedar local al dueno. No compartir estrellas para resolverlo. Los interruptores de transporte, equipo y escenas existentes siguen siendo requisitos; configurar igual en participantes.

## Recorrido del script y resolucion implementada
- 47-842: modelos, aliados, grupos Ballas y auto 492/1450 HP; conservar autoridad/publicacion existentes.
- 4512/inicio (664-1078): SWEET4A, preparacion y salto; regla de rutas T86 + mocap T58, ocultar si falta ruta/clip coherente. No copiar offsets del fork.
- 7599/salida (1078-1449): los tres aliados llenan el auto; dar un 492 por invitado al embarcar el dueno, usando colocacion T40 validada. No ocupar asientos de aliados. Tec9 por canal de equipo existente.
- 1452-1736: llegada a Ballas, GDB_CAR_PLY y camaras de GHETTO_DB: esperar grupo en radio 50 m, copias remotas fuera de plano durante widescreen. No poner a nadie en el auto lleno.
- 1779-2660: cuatro grupos, vida/contador/bajas por Char.IsDead: T46 CPedDamageSync ya lleva da-o de cada tirador al dueno sin duplicarlo; no agregar contador ni paquetes. Ataques siguen nativos; no retarget nuevo.
- 2671-3000: persecuci-n, ayuda y repintado: estrellas propias; no esperar invitados dentro del taller (LOCATE 4/20 m de ambos talleres), los dem-s checkpoints peque-os esperan dentro de 50 m.
- 3010-3267: regreso y GDB_CAR2_PLY: esperar grupo a 50 m, ocultar copias en escena de auto lleno; restauraci-n T86. Auto extra no reemplaza al del dueno.
- 3267-fin: abandono del auto, fallo por aliados/auto, distancia, dialogo, exito y cleanup: conservar logica original y ciclo T40, espejo de HUD/texto/blips y fallo coop existente.

## Casos borde
- 2-4: un conductor por copia, colocacion con piso/pendiente/colision; sin lugar seguro no crear dentro de estructuras.
- Individual F1/abandono: ShouldIncludeNetworkPlayer filtra y cancela embarque pendiente; no sumar ni mover al excluido.
- Muerte/arresto: fallo de mision comun existente; desconexion no deja una espera sobre el jugador ausente.
- Otra area o lejania: no embarcar desde otro interior ni desde lejos; marcadores esperan al participante valido.
- Interruptor coop-drive-by apagado: ninguna excepcion T92, comportamiento previo.

## Aceptacion / resultado
- Implementados los puntos anteriores con sistemas existentes, sin cambios de protocolo ni SCM. No se porto codigo externo nuevo: reutiliza el port MIT T58/T86 ya atribuido en NOTICE.md.
- GTA-reversed consultado: World.cpp::FindGroundZFor3DCoord (0x5696C0), TestSphereAgainstWorld (0x569E20), GetIsLineOfSightClear (0x56A490); Entity/Ped/Ped.cpp::GiveWeapon (0x5E6080), CharacterCommands.cpp::IsCharDead (IsStateDeadForScript), RunningScript::UpdateCompareFlag. Los comandos de creacion/heading/color son los mismos del sistema T40; SET_CAR_HEALTH conserva 1450 del auto de SWEET4.
- xmake build client, server y testbot: OK. Recompilacion forzada final del cliente registrada en build/t92-client-build.log.
- No existe tools/correr_testbot.bat en este clon: ejecutado equivalente aislado en build/t92-testbot, puerto 34992, sin UPnP/persistencia; testbot all: 12 escenarios/121 chequeos TODO PASS. Reinicio con handover=0 y testbot d: 3 chequeos TODO PASS. Servers propios terminados al finalizar.
- Documentados pruebas, sistemas, interruptor y pendiente. CRLF verificado en los seis archivos C++ tocados.
- Sin revisar / sin probar en juego / sin instalar. Pendiente calibracion visual de SWEET4A, escenas GHETTO_DB, disparos y 3-4 autos por el usuario.

## Limites y configuracion
- No se fuerzan invitados desde otro interior o a mas de 120 m. Invitados que ya usan vehiculo conservan ese vehiculo y reciben equipo; quienes estan a pie reciben su copia al embarcar el dueno. Si no hay piso/sitio seguro, T40 no crea el auto (log rule-guest-vehicle); no hay posicion insegura de respaldo ni reintento automatico dentro del mismo intento.
- La municion requiere mission-player-equipment; el dano compartido requiere ped-damage-authority; escenas requieren cutscene-partners. Toggles iguales en participantes. La validacion y cancelacion del embarque reutilizan T40.
- Cambios de pruebas/documentacion no afirman que la mision este arreglada en juego.
