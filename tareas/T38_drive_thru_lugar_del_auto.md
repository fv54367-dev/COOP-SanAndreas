# Tarea T38_drive_thru_lugar_del_auto: el auto de los invitados sale cerca del host y en lugar seguro
Lee `tareas\COMUN_reglas.md` y el resultado de T37 en `tareas\TANDA_2026-11-09_claude.md`. Sin cambios de protocolo. No toques `main.scm`.

## Pedido del dueño
El auto de T37 tiene que aparecer unos metros atras o al costado del host, nunca en cualquier parte, y con cuidado de no meterlo en zonas sin terreno ni dentro de una estructura.

## Problema de T37
`OnDriveThruCarMoved` corre en `SET_CAR_COORDINATES`, antes de que SWEET3 haga `Car.SetHeading($sweet_car, 267.3)`: `FindFreeCarPosition` calcula "atras" con la orientacion vieja del auto.

## Cambio
1. Dispara con `SET_CAR_HEADING` (0x0175) de `SWEET3`, con la misma comprobacion de posicion (<=3 m de 2396.36, -1917.96) y heading cercano a 267.3. Una vez por mision, como ahora. Verifica en `SWEET3.txt` que ese sea el orden y que el `SetHeading` se ejecute siempre que se ejecuta el `SetCoordinates`.
2. Volve `FindFreeCarPosition` a como estaba antes de T37 (sin `found` ni `avoid`) y agrega una funcion propia para este caso. Candidatos en este orden, relativos al auto ya girado: 6 m atras, 9 m atras, 4 m a la derecha, 4 m a la izquierda, 6 m atras + 4 m a cada lado, 9 m atras + 4 m a cada lado. Ninguno a mas de 12 m del auto del host.
3. Un candidato vale solo si cumple todo:
   - Hay suelo en el centro y en las dos puntas del auto (+-2.5 m sobre su eje), buscando desde poco arriba del nivel del auto, y los tres suelos estan a menos de 1 m del nivel del auto del host y a menos de 0.6 m entre si. Esto descarta vacios, techos bajos y escalones.
   - Linea de vista libre (edificios y objetos) desde el auto del host hasta el candidato, a media altura, para que no haya una pared en el medio.
   - Sin colision con edificios, autos, peds ni objetos en todo el largo del auto, ignorando el auto del host.
   - A mas de 10 m de 2411.31, -1928.84 (donde aparece el enemigo).
4. Si ninguno vale: no crear el auto, log `rule-drive-thru-guest-car guests=%d cars=0 reason=no-spot`. Nunca un lugar de respaldo sin comprobar.
5. Crea el auto mirando a 267.3, y en el log agrega la posicion elegida.

## Aceptacion
Compilan client y server. CRLF. Diff acotado. Corregi la linea de T37 en `TESTEAR_HOY.md` si hace falta y 3-5 lineas de critica en la tanda. No instales ni toques los GTA. Resumen <=8 lineas.
