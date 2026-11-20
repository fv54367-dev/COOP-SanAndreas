# T78 - TP por nombre en modo prueba
Objetivo: /tp y /ir por nombre sin consentimiento en test-mode; coordenadas y T60 normal intactos.
Archivos: client/src/CTestMode.cpp, client/src/CChatCommands.cpp, TESTEAR_HOY.md, ESTADO.md y esta tanda.
Restricciones: CRLF C++, sin protocolo, hooks, SCM, instalacion ni juego. Reusar TeleportLocalPlayer.
Aceptacion: client/server/testbot compilan; testbot TODO PASS; resumen <=10 lineas.
Casos borde:
- 3-4 jugadores: exacto prioritario y prefijo unico sin distinguir mayusculas; ambiguo enumera candidatos (incluye local).
- F1 individual y misiones conservan las facilidades existentes del modo prueba.
- Desconectado/sin ped o uno mismo: mensaje sin mover; muerte conserva comportamiento previo de prueba.
- Interior y distancia: posicion y area del ped remoto por el camino nativo ya existente.
- test-mode apagado: pedidos Y/N T60 intactos; /ir vacio conserva jugador cercano en prueba.
Estado: implementado, SIN REVISAR / SIN PROBAR / SIN INSTALAR.

Implementado: busqueda exacta prioritaria/prefijo unico incluyendo nombre local, errores y area del destino; ayuda y pruebas T60 actualizadas. Sin cambios de server/protocolo.
Validado: client -r 39.906 s, server -r 9.25 s, testbot -r 2.875 s; CRLF y diff --check OK. Testbot TODO PASS: 9 escenarios/90 chequeos + d/3 chequeos, cero fallas. Copia temporal del runner con carpetas y kill limitados al repo por sandbox. Limpieza de esos temporales rechazada por politica automatica; quedaron sin versionar.
