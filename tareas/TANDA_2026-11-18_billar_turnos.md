# Tanda 18-11 — turno y tiro del invitado en billar

- El build local-6a91bfd inicia POOL2. En el log, el turno 1 pasa de apuntar (5) a preparar el golpe (7) y a movimiento de bolas (8). El invitado reporta que no se distingue su turno ni la preparación del tiro.
- Objetivo: mostrar el turno de cada jugador, aplicar la toma nativa del tirador y reproducir la postura y retroceso del taco del invitado. CCoopPool, evento de dibujo y lista de pruebas. Sin SCM ni cambios en el protocolo.
- El retroceso se lee del tiempo normalizado de la animación nativa, tal como lo controla POOL2. La física y la potencia final siguen en el script del dueño.
- Casos borde: mientras cambia el turno o se pierde un estado no se anticipa un tiro. Espectadores, modo individual, F2, salida con F7, desconexión y muerte no dejan HUD ni animación de billar activa. Solo el par aceptado juega. Con el interruptor apagado se usa el billar nativo.
- Aceptación: jugar hasta que el turno cambie al invitado. Ambos ven a quién le toca. El invitado ve su CJ apuntar y mover el taco desde la toma de tiro y puede hacer un golpe suave y uno fuerte. Verificar que el host conserva su turno y que F7 restaura la vista.
