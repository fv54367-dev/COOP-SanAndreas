# T65 - Fotos del invitado
Objetivo: ejecutar T65_fotos_del_invitado.md, scripts originales, sin SCM ni instalar.
Reserva #32. Archivos: client/src/CMissionPhotos.{cpp,h}, CObjectSync.{cpp,h}, Hooks/PlayerHooks.cpp, CAimSync.cpp, Commands/CommandHooks.cpp, CConfigLoader.{cpp,h}, client/src/PacketHandlers/MissionPhotos.cpp, server/src/PacketHandlers/MissionPhotos.cpp, shared/network/packets/mission_photos.h, shared/network/packet_types.h, shared/config.h, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md, NOTICE.md y esta tanda.
Restricciones: CRLF, no SCM, no instalacion ni juego; resumen <=10 lineas. Aceptacion: client/server/testbot compilan; tres pruebas documentadas.
## Critica previa
La referencia usa host fijo, sin intento ni participantes: adaptar a autoridad, sesion y filtros. No reenviar flags viejos de fotos fuera de mision. No inventar proyeccion/visibilidad: ejecutar TakePhotograph original. T64 parcial solo aporta identidad de objetos, no interacciones.
## Casos borde
- 3-4 jugadores: agrupar fotos simultaneas por entidad y hora de captura; permitir reintentos posteriores.
- F1 individual/abandono/muerte/desconexion: invalidar sesion; mantener fallo colectivo existente.
- Interior: solo entidades y participantes en la misma area; lejanos sin copia no cuentan.
- Sin copia de entidad de mision no se puede fotografiar en ese cliente.
- Fin/reintento/carga: epoch nuevo, rechazar eventos viejos; flags preexistentes no cuentan como foto nueva.
- coop-photos=false conserva TakePhotograph anterior; coleccionables siguen CCollectibleSync.
## Estado
Implementacion terminada. SIN REVISAR / SIN PROBAR / SIN INSTALAR.

Reserva adicional #33: tools/testbot/testbot.cpp; escenario i de serializacion/ruteo real con cuatro clientes.

## Implementado
- CMissionPhotos + MISSION_PHOTO fiable al final del protocolo; 0.4.31-alpha. Interruptor coop-photos=true independiente de T63/T64. Call-site existente de TakePhotograph; sin direcciones/hook nuevos.
- Técnica MIT Objects.cpp::HookTakePhotograph/HandlePhoto, citada en NOTICE.md. Nativo del juego determina visibilidad/LOS/encuadre; fotos previas no se confunden con disparo actual. Se conservan flags locales y coleccionables.
- Autoridad/epoch por intento, anuncio 500 ms y caducidad invitado 1500 ms; filtro individual/abandono/muerte/participante/área y originales de misión. IDs ped completos 64 bits; objetos uint32 de CObjectSync. Sin copia no se puede fotografiar allí.
- Corrección al plan: deduplicar una entidad para toda la misión rompe fotos posteriores tras una foto descartada. Agrupación de marcas en receptor durante 500 ms por entidad; disparos posteriores pueden rearmarla. La ventana usa hora compartida de captura (g_serverTime), no llegada: rechaza fotos anteriores a la ultima aplicada y eventos de mas de 5 s; tolera hasta 1500 ms de adelanto por ajuste del reloj. No se fuerza éxito si el script descarta por otras condiciones (SYN1).
- Auditoría verificada en main.scm original local: BCRASH1 inicio 122B3C, HAS_CHAR 1257DF/125BD1 (284@); SYN1 HAS_CHAR 17CDE2 y otras 15 consultas (391@/406@/436@/420@); HEIST1 HAS_OBJECT 22F480/230FAE (107@). Solo lectura; SCM intacto.
- Archivos extra coordinados: tools/testbot/testbot.cpp (reserva #33), docs/SISTEMAS.md y tanda Karimov (reserva #34). Testbot i con cuatro clientes reales ENet: 9 chequeos, cero fallas; logs build/t65_network.log. Prueba ruteo/identidad/remitente/F1/fin, no ejecuta GTA ni valida encuadre/misiones.
- Client inicial OK 19.906 s, server OK 8.5 s, testbot OK 2.812 s; reconstrucción final/CRLF al cierre.
- SIN REVISAR / SIN PROBAR / SIN INSTALAR. No se instala, juega ni publica. .git es solo lectura: no se puede crear commit en este entorno. Preservar archivo ajeno T68.
- Commit sugerido: `feat: compartir fotos de misión de los invitados`; cuerpo: `Adaptado de sa-coop Karimov-Akbar (MIT), features/mission-objects/Objects.cpp. Semántica gta-reversed Weapon::TakePhotograph 0x73C1F0 y HasCharBeenPhotographed. Protocolo 0.4.31-alpha.`

## Verificacion final
- Cliente forzado final: OK 39.36 s (build/t65_client_rebuild_final.log); server forzado: OK 8.797 s (build/t65_server_rebuild.log); testbot final OK 3.172 s (build/t65_testbot_final.log).
- Tras agregar hora de captura al paquete, ENet real repetido: 9 chequeos PASS, cero fallas (build/t65_network_final.log). Servidor aislado cerrado al finalizar; no se toco ningun GTA ni instalacion.
- CRLF OK en 15 archivos C++/headers; git diff --check OK. Pruebas manuales y revision cruzada pendientes, no se afirma resultado en juego. Reservas #32-34 liberadas; commit pendiente por .git de solo lectura.
