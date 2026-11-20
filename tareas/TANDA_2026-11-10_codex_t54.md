# T54 — Life's a Beach coop
Objetivo: extender el módulo/paquete T53 para DANCE nativo, sin SCM ni instalación.
Archivos: client/src/CCoopLowrider.cpp, client/src/CConfigLoader.{cpp,h}, shared/network/packets/scripts.h, shared/config.h, ESTADO.md, TESTEAR_HOY.md, INTERRUPTORES.md, docs/MISIONES_MODO.md, esta tanda y TANDA_2026-11-10_sacoop_karimov.md.
Restricciones: CRLF C++; MIT citado; un solo dueño de protocolo, 0.4.25-alpha.
Aceptación: client/server/testbot compilan; coop-dance=true; resumen ≤10 líneas.
Crítica previa: separar sesión/tipo de concurso; no confundir DANCE de clubes con MUSIC1; deduplicar puntaje acumulado y retener final máximo 3 s; limpiar DANCE antes de devolver cámara.
Casos borde:
- 3–4: lugares por orden de id y suma independiente por participante.
- F1 individual, interruptor apagado, otra área o >40 m: no arrancar baile.
- Abandono/desconexión: termina baile local, deja de sumar sin fallar misión; muerte/arresto conserva fallo colectivo.
- Otros usos de DANCE quedan intactos; cierre o cambio de autoridad solicita salida nativa.
- Puntaje final confiable y sesión rechazan mensajes atrasados; espera acotada por invitados desconectados.

## Implementacion
- Extiende CCoopLowrider y COOP_LOWRIDER de T53 con kind=2 y 30 cámaras; no duplica handlers ni agrega paquetes. Protocolo 0.4.25-alpha.
- Adaptado de sa-coop (Karimov-Akbar, MIT): src/client/features/story/minigames/Minigames.cpp KIND_DANCE. Licencia en NOTICE.md existente.
- Valida hilo externo al inicio (gta-reversed StreamedScripts.cpp 0x470890); solo MUSIC1. DANCE local calcula puntos contra su música.
- Suma acumulada monotona por sesión/participante; captura tambien ultimo juicio por opcode, espera final maxima 3 s y rechaza puntos tras resolver.
- Abandono conserva puntos ya aportados y deja de sumar; muerte/arresto mantiene fallo colectivo. Salida nativa con iTerminateDanceMiniGame, sin terminar otros DANCE.
- CJ remoto usa sincronización existente: visibilidad de pasos pendiente de prueba. No se instala ni se ejecuta GTA.

## Verificacion y entrega
- Client completo con -r OK (31.953 s); server OK (6.297 s); testbot OK (2.187 s).
- Testbot canonico con rutas y cierre limitados a build/t54_testserver*: 5 escenarios, 22 chequeos, cero fallas, TODO PASS. No cubre el baile: requiere prueba del usuario.
- CRLF C++ y git diff --check verificados; pendiente revision cruzada y juego.
- Commit bloqueado: git add no puede crear .git/index.lock (Permission denied); .git esta en solo lectura por el entorno, sin elevacion disponible. Sin commit ni push.
- Mensaje sugerido: feat: baile de Life's a Beach en equipo (SIN REVISAR). Cuerpo: Adaptado de sa-coop (Karimov-Akbar, MIT): src/client/features/story/minigames/Minigames.cpp KIND_DANCE; gta-reversed StreamedScripts.cpp StartNewStreamedScript (0x470890).
