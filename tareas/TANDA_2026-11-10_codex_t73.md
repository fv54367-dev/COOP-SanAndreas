# T73 - correcciones verificadas de revision GLM
Objetivo: corregir hallazgos 1-13, 15-18, 20-21, 25-26; excluir 14,19,22-24.
Archivos: client/src/{CMissionMachines,CSocialActions,CVoice,CCoopLowrider,CCoopPool,CCutscenePartners,CMissionGuestActions,CGpsRoute,CSceneMocap,CMissionPhotos}.cpp, server/src/CInternet.cpp, server/src/PacketHandlers/scripts.cpp, shared/network/packets/scripts.h, tools/test_t73.py, tareas/REVISION_GLM_2026-11-10.md y esta tanda. Reserva #47.
Pendiente coordinar por reservas T74: shared/config.h, ESTADO.md, TESTEAR_HOY.md.
Restricciones: CRLF C++; no instalar, no jugar, no SCM; resumen <=10 lineas.
Aceptacion: client/server/testbot compilan y runner canonico TODO PASS.
## Critica previa y casos borde
- Sesiones deben vincular due?o, invitado, tipo e intento; no aceptar puntos antes de JOIN ni tras DONE; caducar sin permitir replay por heartbeat.
- 3-4 jugadores: estado independiente por invitado; individual, muerte, desconexion o cambio de area invalidan participacion.
- Limpieza diferida de maquinas sobrevive Reset/apagado; no restaurar estado ajeno si cambia la identidad del ped al revivir.
- Pool invalido se rechaza antes de desreferenciar; la validez de puntero no prueba identidad tras reutilizar slot: capturar refs cuando haya llamadas al engine.
- Billar reserva partner vence separadamente del OFFER; mouse por frame no se descarta con rate-limit: limitar relay sin perder suma.
- Interruptores apagados conservan comportamiento anterior salvo finalizar limpieza propia pendiente.
- GPS no libera pedidos de scripts: al perder propiedad abandona su estado y espera que termine el pedido externo.
