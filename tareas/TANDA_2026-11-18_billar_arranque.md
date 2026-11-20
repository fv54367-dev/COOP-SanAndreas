# Tanda 18-11 — arranque del billar de a dos

- Objetivo: llegar al primer tiro en POOL2, conservar la sesión durante la preparación y dibujar mesa, tacos y bolas en ambos GTA. El informe en ESTADO.md registra que F7 y VIEW llegaron, pero POOL2 terminó tres segundos después.
- Archivos: CCoopPool.cpp, handler COOP_POOL del server, escenario F del testbot y lista de pruebas. Sin cambios en el formato ni en los canales de paquetes, sin editar SCM. Referencia MIT: sa-coop Pool.cpp (4135fc5).
- Se exige proximidad para oferta, aceptación y arranque. Durante la partida el par reservado depende de la sesión, la actividad y el área local, no de cada posición replicada durante la animación. Los espectadores conservan el filtro de cercanía.
- Casos borde: F7, desconexión, muerte, cambio de área o F1, otra misión local del invitado y pérdida sostenida de paquetes terminan o limpian la sesión. Con el interruptor apagado se mantiene el billar nativo. Una pérdida temporal de posición no reemplaza al invitado ni permite que un espectador mande controles.
- Aceptación: compilar cliente, server y testbot. Escenario F cubre el estado del par con posiciones fuera del radio y el filtrado de espectadores. En juego, probar con dos GTA más de diez segundos, romper el triángulo y salir con F7. La apuesta queda para después de que se pueda jugar la partida.
