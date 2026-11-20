# T86: reglas de escena para invitados

Objetivo: corregir presencia/recorrido en Big Smoke, Cleaning the Hood, Drive-Thru y Emmet.
Archivos reservados por tanda: client/src/CScenePartner.cpp, client/src/SceneCompat/ScenePartner.h, client/src/CCutscenePartners.cpp, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Restricciones: CRLF C++; sin SCM, protocolo, instalacion ni GTA; Red tablero no responde, seguir tanda en serie.
Aceptacion: client/server/testbot compilan, testbot TODO PASS, pruebas documentadas; resumen <=10 lineas.

Critica previa (solo lectura): el plan original no cubre fin de recorrido dentro de un plano, rutas distintas de los rangos 1/2, falta de mocap, ni el final Grove dirigido por script (no CCutsceneMgr). Drive-Thru es SWEET3 / SWEET2B; Nines es SWEET2 / SWEET3B. Una pose por plano no basta: validar cada frame usando el reloj de animacion, sin cambiar protocolo. No mover el auto real del jugador para decorar la toma.

Casos borde:
- 2-4: asiento por rango; sin asiento se oculta; autos decorativos separados por rango.
- F1 individual/abandono/espectadores/interior distinto: solo elegibles y participantes, bajo cutscene-partners existente.
- Fin/salto/desconexion/muerte/cambio de elenco: borrar decoracion y restaurar visibilidad por handle vigente.
- Sin recorrido/mocap: ocultar los tramos que requieren caminar; no congelar una pose visible.
- Auto de entrada: recordar antes de cargar; sin dato verificable se oculta, nunca inventar entrada en auto.
- Apagado: camino previo de cutscene-partners=false; no sistema ni interruptor nuevo.

## Implementado y verificado
- Reutilizados POSE_WAY/WALK/CAR/HIDDEN, HoldInCar, rutas embebidas y Restore/ReleaseAll. Tabla SceneRule por nombre, reevaluada por fotograma y rango, no solo por plano. PlacesOf normaliza mayusculas.
- Big Smoke: escena de auto por script (INTRO1, mision 11), fuera de CCutsceneMgr; filtro Grove/area 0/widescreen y Perennial 404. Asientos nativos libres por rango ordenado; dueno conserva asiento real. Cleaning final (14) usa el mismo camino pero oculta. Solo remotos participantes/elegibles/cercanos.
- Drive-Thru: registro de auto/modelo/colores/ped-handle al entrar a mision 15. Auto local por rango construido con CAutomobile, sin colision, control por matriz y sin call-sites SCM/trafico de WorldHooks; borrar por handle+puntero al restaurar. Separacion 2 m entre carrocerias; no traslada autos reales ni cambia protocolo. Sin dato, modelo cargado, hueco de pool o prop fiable: ocultar. Autos de entrada considerados: automoviles normales; bicis/motos/barcos/aviones sin asiento de automovil coherente se ocultan.
- Emmet SWEET3B: rutas por rango y track de CJ cuando continua fuera de ruta. Sin mocap/ruta valida, ocultacion conservadora. INTRO1B oculta el tramo no acompanado por su ruta. No cambiar las rutas embebidas ni extraer clips nuevos: ya existen.
- Evidencia: gta-reversed Entity/Ped/Ped.cpp GetSeatPositionInVehicle (dummies delanteros/traseros y lado), SetPedPositionInCar; Models/VehicleModelInfo.h GetFrontSeatPosn/GetBackSeatPosn; Entity/Vehicle/Automobile.cpp constructor; CutsceneMgr.cpp carga/nombre/reloj. API existente de animacion y pose; sin direcciones/hooks nuevos.
- Client final recompilo afectados/enlazo OK (2.907 s); server -r OK (9.844 s), testbot -r OK (3.641 s). Runner aislado build/t86_testbot.bat, puertos 6897/6898 y solo sus servidores, exit 0. build/t86_testbot.log: 11 escenarios/110 chequeos + handover apagado 1/3 = 12/113, cero fallas, TODO PASS. CRLF de los tres C++/header y git diff --check OK.
- ESTADO y TESTEAR_HOY actualizados. Sin instalar ni abrir GTA. Todo SIN revision Claude y SIN prueba visual: calibrar encuadres/dummies/rutas/auto detras con el dueno. Commit queda al orquestador: .git de esta sesion es solo lectura.
