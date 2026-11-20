# T64 — Home Invasion cooperativo

Objetivo: ejecutar tareas/T64_home_invasion_coop.md sobre scripts originales, sin SCM ni instalación.
Reserva #29. Archivos previstos: CHomeInvasion.{cpp,h}, CObjectSync.{cpp,h}, CMirrorBlips.cpp, CMissionGuestCounts.{cpp,h}, handlers HomeInvasion, shared/network/packets/home_invasion.h, packet_types.h, config.h, CConfigLoader.{cpp,h}, NOTICE.md, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md y esta tanda. Compartidos coordinados por la tanda Karimov.
Restricciones: CRLF C++; sin jugar, instalar ni publicar. Resumen <=10 líneas. Criterio: client/server/testbot compilan y cuatro pruebas manuales documentadas; revisión cruzada y juego pendientes.

## Crítica previa
- T63 no redirige acciones posteriores sobre CJ: no basta ampliar LOCATE para entregar cajas.
- La variable de caja activa del script es única; comprobar su continuación antes de admitir varias cajas simultáneas.
- No confundir autoridad de misión con host; identidad y arbitraje deben durar solo el intento actual.
- El log disponible del receptor sí registra siete blips type=3, kind=0, display=3, form=follow (t=15544062–15544109): recepción demostrada, invisibilidad todavía sin causa probada.

## Casos borde
- 3–4 jugadores: arbitraje por caja, nunca duplicar entrega ni retirar la del dueño.
- Individual/F1, abandono y desconexión: excluir, soltar cajas y restaurar ropa propia.
- Muerte/arresto: conservar fallo colectivo; no guardar referencias colgantes.
- Interior: caja y portador comparten área al cruzar, ruido solo en la casa.
- Fin/reintento/carga: limpiar identidad, ropa, HUD y blips; rechazar paquetes viejos.
- coop-home-invasion apagado conserva el comportamiento anterior.

## Estado
**PARCIAL: no cumple todavía la aceptación completa de T64.**
- Implementado únicamente blips de cajas con identidad CObjectSync. Al tener copia válida, usa BLIP_OBJECT nativo con display original; antes era FOLLOW/BLIP_COORD radar-only. Fallback mientras no llega el objeto y reconciliación por pool ref al aparecer/desaparecer. Interruptor nuevo coop-home-invasion=true; apagado conserva la ruta anterior.
- Archivos efectivamente tocados: CConfigLoader.{cpp,h}, CObjectSync.{cpp,h}, CMirrorBlips.cpp, CMirrorBlipsPolicy.h, shared/network/packets/mirror_blips.h, shared/config.h; esta tanda, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md y tools/{inspect_t64,test_t64_blips}.py. El resto de los previstos NO se implementó. Protocolo 0.4.29 -> 0.4.30-alpha (nuevo entityKind OBJECT=4, id uint32; no paquete nuevo).
- Referencias: gta-reversed Radar.cpp::Draw3dMarkers, DrawCoordBlip y DrawEntityBlip. La recepción de siete objetos display=3 está demostrada en el log del receptor; la conversión a FOLLOW explica la falta de flechas 3D, no demuestra por sí sola la ausencia en radar reportada. Falta verificar visualmente esa parte.
- Consultada la referencia MIT Karimov-Akbar/sa-coop Objects.cpp (Held/TakeUp/HandleHold) y Missions.cpp (CrateAnswer/ropa/UpdateOwnNoise); NO se portó su código de interacción ni se afirma haberlo implementado. Licencia MIT ya incluida en NOTICE.md por T53.
- GUNS1 original local: misión 24, inicio 0x0B69A0, longitud 41159, FNV-1a DA1FBDD4. Holding: BD021, BD119, BD63F; entrega LOCATE BD1DE y botón BD264; ruido tras conversión global BAD5A, comparación BAD66. tools/inspect_t64.py permite revisar los tramos sin modificar SCM.
- Corrección de la crítica inicial: la entrega BD271–BD697 NO mueve ni congela a CJ: actualiza 35@/50@/48@/47@, diálogos/tareas de Ryder y elimina la caja. Sí usa estado único 36@/60@: requiere seleccionar/serializar entregas de cajas de varios portadores sin sobrescribir al dueño. La referencia sola no prueba ese caso.
- Pendiente TODO el arbitraje/propiedad y transporte de cajas, filtrado de participantes/epoch de interacción, entrega y deduplicación, restauración de ropa y ruido propio. No se habilitaron condiciones T63 de GUNS1 sin esa propiedad.
- Client build OK (25.016 s), server OK (7.266 s), testbot OK (2.484 s). tools/test_t64_blips.py compila política real con g++ y comprueba resolución/fallback/marker/cambio de pool/remoción y tipos previos; no ejecuta GTA ni prueba entrega. CRLF/diff se verifican al cierre.
- SIN REVISAR / SIN PROBAR / SIN INSTALAR. No se instala, juega ni publica. Sin commit: .git es solo lectura en este entorno. Cambios ajenos en T67 se conservan.
- Cierre: reconstrucción forzada final del cliente OK (38.594 s, build/t64_client_final.log); server/testbot relink OK tras ajuste de comentario/orden de constantes. Política aislada vuelve a pasar. CRLF OK en ocho archivos C++/headers, diff --check sin errores; aviso LF ajeno en T67. Reservas #29–31 liberadas. T64 sigue PARCIAL.

T68 (2026-11-10): detenida por la salida prevista en la tarea; serializacion no implementada. Puerta 60@/51@ anterior al LOCATE y WAIT dentro del borrado requieren estado persistente y propiedad de caja. Evidencia y continuacion: `tareas/TANDA_2026-11-10_codex_t68.md`, `tools/inspect_t68.py`. T64 sigue parcial; ropa/ruido/entrega pendientes.
