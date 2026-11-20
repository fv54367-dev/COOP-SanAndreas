# T87: puerta visible en cinematica

Objetivo: diagnostico persistente y ocultacion local de duplicados de REMOVE.
Archivos: client/src/CCutsceneHidden.cpp, client/src/CCutsceneHidden.h, client/src/Hooks/GameHooks.cpp, client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Restricciones: CRLF C++; sin SCM/protocolo/instalacion/juego; Red no responde, coordinacion por tanda.
Aceptacion: client/server/testbot compilan; TODO PASS; resumen <=10 lineas.
Casos borde:
- 2-4 jugadores y F1: cada proceso oculta solo lo pedido por su propia escena, sin paquetes.
- Salto, muerte, desconexion o fin: restaurar al salir de la escena; referencias nativas estables y handles de pool contra borrado/reuso.
- Interiores/lejanos: mismo modelo a <=2 m de REMOVE, sin tocar entidades exclusivas alejadas ni CCutsceneObject.
- Entidades creadas/reveladas durante escena: reevaluar por frame; no restaurar las originalmente invisibles.
- Interruptor apagado: ocultacion nativa intacta, diagnostico activo.
Critica: el contador REMOVE se reutiliza para referencias ocultas; capturar antes de HideRequestedObjects, no interpretar el contador posterior como solicitudes. Diagnostico completo por pools, sin limite arbitrario de resultados.

## Implementado
- Nuevo CCutsceneHidden, integrado desde GameHooks; opcion cliente cutscene-hide-extras=true. Sin tocar CObjectSync: su API identifica objetos enviados/copias en el diagnostico.
- Hook de llamada 0x5B176A validado por opcode/destino; exe 1.0 US desensamblado con capstone/pefile confirma HideRequestedObjects y reset del contador en 0x5AFADD (omitido por gta-reversed). Copia de REMOVE antes del nativo.
- Log sin cupo de 400 lineas del logger viejo: native-hide, inicio y +2 s, todas las entidades de cada modelo a 30 m por pools. found/native-found, modelo, coordenadas, puntero, tipo/objectType, area, visibilidad y origen conocido (sent/copy/decor/map-pool desconocido).
- Por frame: ocultar entidades visibles a 2 m usando el bit de visibilidad nativo; exclusion objectType CUTSCENE y lista del decorado. Solo registrar entidades originalmente visibles; referencias en nodos estables de lista y handle de pool para restauracion y borrado/reuso. Se mantiene la ocultacion si otro sistema vuelve a mostrarlas. Fin/salto/cambio de nombre/datos descargados restaura y libera.
- Referencias: gta-reversed CutsceneMgr.cpp HideRequestedObjects/DeleteCutsceneData; Entity.cpp RegisterReference/CleanUpOldReference (algunos buildings no registran; por eso tambien validar handle); DummyObject.cpp CreateObject/UpdateFromObject, Object.cpp constructor desde dummy, CutsceneObject.cpp tipo CUTSCENE=4.
- Client build inicial OK (29.797 s), final incremental afectado y enlace OK (1.718 s); rebuild completo client -r OK (49.657 s). Server -r OK (10.625 s), testbot -r OK (3.891 s).
- Runner aislado build/t87_testbot.bat, puertos 6907/6908, solo sus servidores: exit 0; 11 escenarios/110 chequeos + 1/3 sin handover = 12/113, cero fallas, TODO PASS (build/t87_testbot.log).
- Cinco archivos C++/headers CRLF verificados; git diff --check OK. ESTADO, TESTEAR_HOY e INTERRUPTORES actualizados. Sin instalar/abrir GTA. SIN REVISAR por Claude y SIN PROBAR visualmente; causa T21 pendiente de evidencia nueva.
- Commit a cargo del orquestador: .git es solo lectura en esta sesion. No se intento publicar.
