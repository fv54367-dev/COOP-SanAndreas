# T84: CJ local tras ropa y peluqueria

Objetivo: regenerar el TXD player local antes de CSPLAY sin cache incremental.
Archivos: client/src/Hooks/CutsceneClothesHooks.cpp, client/src/Hooks/CutsceneClothesHooks.h, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Restricciones: CRLF C++; sin protocolo, SCM, instalacion ni GTA. Red reservar no respondio; continuidad documental en serie de T83, siguiendo tandas.
Aceptacion: client/server/testbot compilan, TODO PASS, diff/CRLF; responder resumen <=10 lineas. Revision de Claude y juego pendientes.

Casos borde:
- 2-4 jugadores: siempre FindPlayerPed(0), independiente del ultimo remoto vestido.
- F1 individual, interiores y distancia: misma ropa propia; sin reglas de mision ni autoridad nuevas.
- Muerte/desconexion durante carga: validar ped/datos/clump; si falta local, conservar llamada vanilla y registrar omision.
- Ropa/pelo identicos a la cache tras tienda: descriptor anterior nullptr fuerza texturas y geometria.
- Reconstruccion remota durante la cinematica: materiales ya creados conservan referencias de texturas; probar cambios de otro jugador y cinem?ticas consecutivas.
- Sin sistema ni interruptor nuevo: correccion del hook T48 existente.

Evidencia: gta-reversed Clothes::RebuildPlayer 0x5A82C0, ConstructPedModel 0x5A81E0, ClothesBuilder::CreateSkinnedClump 0x5A69D0, CutsceneMgr::LoadCutsceneData. El reversed local tiene un ms_textureHasChanged=true incondicional que NO coincide con el exe: capstone del GTA_JUGADOR1 confirma en 0x5A6BB0 puesta a cero cuando stats coinciden y retorno nullptr en 0x5A6C2B si no hay diferencias. RebuildPlayer usa cache global 0xBC1C78. Con oldClothes=nullptr, 0x5A6A91-0x5A6A9F fuerza los tres flags; con bCutscenePlayer=false, 0x5A6AF5 permite ConstructTextures. No copiar ese error de gta-reversed.

## Implementacion
- Se reemplaza RebuildPlayer incremental por ConstructPedModel(modelo local, descriptor local, nullptr, false), seguido inmediatamente de RebuildCutscenePlayer(local, modelId). Actualiza fat/muscle como el nativo; conserva el ped y sus asociaciones (no DeleteRwObject/Dress).
- No requiere cambiar CPersistenceSync ni CNetworkPlayer: el productor remoto sigue vistiendo su ped; el consumidor CSPLAY prepara siempre sus propias texturas. El receptor real de ropa remota es PacketHandlers/Players.cpp:397-414, no CPersistenceSync (este ultimo restaura al local).
- Log cutscene-cj-rebuild: ped elegido/solicitado, modelo destino/origen, head/torso, fat/muscle y full=1; full=0 reason=local-unavailable conserva llamada vanilla.
- Client -r OK 55.235 s; server -r OK 10.843 s; testbot -r OK 4.641 s. CRLF de ambos archivos OK; git diff --check OK.
- Runner existente adaptado en build/t84_testbot.bat para escribir solo dentro del repo y cerrar solo sus servidores. Sin instalar ni ejecutar GTA. Sin commit: .git solo lectura; deja revision/commit al orquestador.

Resultado final: build/t84_testbot.log: 11 escenarios/110 chequeos + handover apagado 1/3 = 12 escenarios, 113 chequeos, cero fallas, TODO PASS (exit 0). Sin prueba visual de GTA.
