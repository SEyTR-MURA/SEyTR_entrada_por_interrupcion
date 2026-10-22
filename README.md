# SEyTR · Entrada por interrupción

Proyecto de ejemplo de **Sistemas Empotrados y de Tiempo Real**. Es el de la
demostración de la sesión **U2-S2**, entradas e interrupciones: un pulsador
cuyas pulsaciones se cuentan leyendo el pin periódicamente o con una
interrupción, y unas trazas que dan el instante de cada flanco en
microsegundos, para que el rebote se vea.

Parte del proyecto de U2-S1, `SEyTR_salida_y_temporizacion`: conserva su
componente `led` y le añade otro para el pulsador. Así se ve también cómo crece
un proyecto por componentes.

No hace falta cuenta ni permisos para clonarlo: el repositorio es público.

El trabajo propuesto de la sesión te pide escribir **tu propio** contador de
pulsaciones, por sondeo y por interrupción. Este proyecto no lo sustituye:
sirve para repetir la demostración y para comparar tus medidas con las suyas.

## Qué hace

Cuenta las pulsaciones y, cada vez que termina una ráfaga de flancos, la traza
entera: cuántos flancos ha habido, el instante de cada uno contado desde el
primero y qué ha decidido el programa de cada uno. Una pulsación de un pulsador
mecánico no es un flanco limpio: el contacto rebota, y lo que se ve es una
ráfaga.

Mientras corre, eliges con una tecla **cómo se entera** el programa de que el
pin ha cambiado y **cómo filtra** el rebote. No hay que recompilar: basta con
pulsar la tecla en el monitor.

- **Lectura del nivel.** Lee el pin cada milisegundo y resume cada medio
  segundo lo que ha visto. Sirve para ver qué pasa sin resistencia de
  polarización.
- **Sondeo.** Lee el pin cada 1 ms o cada 10 ms, esperando hasta instante
  absoluto, y cuenta cada transición al nivel de pulsado. Sin ningún filtro.
- **Interrupción con cola.** La rutina de interrupción anota el pin y el
  instante y los manda por una cola a una tarea, que filtra el rebote y cuenta.
- **Interrupción con notificación.** La rutina de interrupción solo avisa a la
  tarea. El aviso no lleva el instante: la tarea lo toma al despertar.

Con interrupción, el rebote se filtra en la tarea, nunca en la rutina de
interrupción, de dos formas que se pueden combinar: con un **tiempo de
guarda**, que descarta los flancos que llegan demasiado pronto después de uno
aceptado, y con la **confirmación del nivel**, que, pasado el rebote,
comprueba que el pin sigue pulsado.

## Qué necesitas antes

El entorno instalado y verificado con la guía de instalación de la asignatura,
la placa y su cable USB de datos, conectado al conector **USB-a-UART** de la
placa, el del puente USB-serie («USB-to-UART Port» en la guía del fabricante).
Por el otro conector, el USB nativo del ESP32-S3, verás las trazas, pero las
teclas no le llegan al programa. Conecta solo el cable del conector USB-a-UART.

Un **pulsador** montado entre un pin y masa, con la placa desconectada. No
lleva resistencia: la pone el propio ESP32-S3, que tiene una de pull-up
interna en cada pin. Si aún no tienes el pulsador, sirve un cable que se toca
contra masa.

El LED es opcional. Si lo tienes, conmuta con cada pulsación contada.

## Cómo obtenerlo

Clona el repositorio en una carpeta de tu equipo con una ruta **corta y sin
espacios ni tildes**, y abre en el editor la carpeta que contiene el
`CMakeLists.txt` de la raíz. Si abres una carpeta por encima o por debajo, el
entorno no reconoce el proyecto y el error no te dirá eso.

## Antes de compilar: los pines

Los pines se declaran en un solo sitio, `main/pines.h`, y el repositorio llega
**sin pines elegidos**, porque elegirlos es parte del trabajo. Sin pulsador no
hay nada que medir: mientras no elijas su pin, el programa avisa al arrancar y
no hace nada más.

Para elegirlos, abre el esquema de tu placa y la tabla de pines de la hoja de
características del ESP32-S3, y descarta los que sirven a la memoria externa,
los de arranque, los del USB y la consola y los que la placa ya usa. Después
escribe sus números en `PIN_PULSADOR` y, si tienes LED, en `PIN_LED`, y
anótalos en esta tabla:

| Señal | GPIO | Montaje |
| --- | --- | --- |
| Pulsador | *sin elegir* | Entre el pin y masa, con el pull-up interno |
| LED | *sin elegir* | Entre el pin y masa, con su resistencia en serie |

Si montas el pulsador entre el pin y 3,3 V, pulsado es el nivel alto: cambia
`PULSADOR_NIVEL_PULSADO` a 1 en el mismo fichero, y el programa usará el
pull-down interno y el flanco de subida. Para el LED vale lo que en U2-S1, con
`LED_NIVEL_ENCENDIDO`.

## Cómo se usa

Compila, graba y abre el monitor. Al arrancar, el programa imprime la lista de
teclas y empieza leyendo el nivel, con la resistencia de polarización activada.

| Tecla | Qué hace |
| --- | --- |
| `l` | Lectura del nivel cada milisegundo, con un resumen cada medio segundo |
| `s` | Sondeo, sin filtro |
| `t` | Cambia el periodo del sondeo entre 1 ms y 10 ms |
| `c` | Interrupción con cola |
| `n` | Interrupción con notificación directa |
| `0` | Sin tiempo de guarda |
| `1` | Tiempo de guarda corto: 1 ms |
| `2` | Tiempo de guarda razonable: 30 ms |
| `v` | Activa o quita la confirmación del nivel, 20 ms después del flanco |
| `p` | Activa o quita la resistencia de polarización interna |
| `z` | La cuenta vuelve a empezar |
| `h` | Vuelve a imprimir la lista |

Las teclas se pulsan sin Intro. **Cada tecla, salvo `h`, hace que la cuenta
empiece de cero.** El tiempo de guarda y la confirmación solo actúan con interrupción: el
sondeo de este proyecto no filtra nada, a propósito. Las cifras de 1, 30 y
20 ms son las de partida; se cambian en la configuración del proyecto.

## Qué deberías ver

Cada cambio de tecla escribe dos líneas con lo que está en marcha, empezando
por `---`. Después, según la variante:

**Lectura del nivel.** Una línea cada medio segundo:

```
I (12345) entrada: nivel 1; en 500 lecturas: 0 cambios, 0 a nivel de pulsado; pull-up interno activado
```

Con la resistencia activada, en reposo el nivel es 1 y no cambia; con el
pulsador apretado, 0: el pulsador es **activo a nivel bajo**. Con `p` quitas la resistencia
y el pin queda **flotante**: lo que lee ya no está determinado por nada. Puede
quedarse en un valor, cambiar al acercar la mano o al tocar el cable, o ir y
venir. Fíjate en la cuenta de cambios. Vuelve a activarla con `p` antes de
seguir: con interrupción, un pin flotante puede disparar interrupciones sin
parar, y la tarea que las atiende, que es la de más prioridad, no dejaría
tiempo a las demás.

**Una ráfaga.** Con sondeo o con interrupción, cada ráfaga se traza cuando
termina, es decir, cuando pasan 100 ms sin flancos. Tiene esta forma; las
cifras son de ejemplo:

```
I (23456) entrada: rafaga: 5 flancos en 1.512 ms; contadas (*): 1, rebotes (.): 4, sin confirmar (x): 0
           +0 *      +312 .      +540 .      +903 .     +1512 .
I (23457) entrada: pulsaciones en total: 3; eventos perdidos por cola llena: 0
```

Los números son microsegundos desde el primer flanco de la ráfaga. La marca de
detrás dice qué ha decidido el programa de cada uno: `*`, pulsación contada;
`.`, rebote descartado por el tiempo de guarda; `x`, descartado porque el
nivel no se confirmó. Lo que merece la pena mirar:

- **Con sondeo cada milisegundo**, pulsa cinco veces. Si tu pulsador rebota
  durante más de un milisegundo, la cuenta puede salir por encima de cinco:
  basta con que una lectura caiga en uno de los saltos del rebote. Con `t`,
  cada 10 ms, el sondeo ve menos transiciones del rebote, pero ve el pin con
  hasta 10 ms de retraso.
- **Con interrupción y sin tiempo de guarda** (`c` y `0`), pulsa cinco veces:
  el rebote sigue ahí, y se ven todavía más flancos, porque la interrupción
  atiende transiciones que el sondeo, entre una lectura y la siguiente, no
  llega a ver. El rebote no es cosa del sondeo. Aquí puedes **medir el
  rebote de tu pulsador**: cuánto dura la ráfaga y cómo se reparten los
  instantes. Mira también si hay ráfagas al soltar.
- **Con tiempo de guarda**, prueba los dos valores, `1` y `2`. El corto deja
  pasar el rebote que dura más que él; el razonable lo descarta. Es un
  parámetro que se ajusta midiendo, no una constante.
- **Mantén el pulsador apretado un segundo** con la guarda razonable. Si tu
  pulsador también rebota al soltarse, la suelta llega cuando la guarda ya ha
  pasado y cuenta como una pulsación más. Lo mismo ocurre con cualquier
  pulsación que dure más que la guarda, no solo con la de un segundo. Activa
  entonces la confirmación con `v`: el flanco de la suelta aparece con `x`,
  porque 20 ms después el pin ya no está pulsado.
- **Con notificación** (`n`), el comportamiento con la guarda y la
  confirmación es el mismo, pero la traza dice otra cosa: el instante es el
  del momento en que la tarea despertó, no el del flanco, y entre paréntesis
  sale cuántos avisos se habían acumulado. Anota qué información has dejado de
  tener respecto de la cola.

**Eventos perdidos.** Si la cola se llena, el envío desde la rutina de
interrupción falla y el evento se pierde; el programa lo cuenta en la línea
del total. Con la longitud de partida, 32, haría falta una ráfaga de más de 32
flancos mientras la tarea está ocupada. Para provocarlo,
baja la longitud de la cola a 2 en la configuración del proyecto y, con `c`,
`0` y `v`, pulsa: la tarea espera 20 ms en cada flanco antes de confirmar, y
mientras tanto la cola se llena.

Si el monitor muestra las trazas pero no responde a las teclas, mira el
conector: tiene que ser el USB-a-UART.

## Qué puedes cambiar sin tocar el código

En la configuración del proyecto, en el menú **«SEyTR - Entrada por
interrupcion»**, que está en el nivel superior, junto a los de ESP-IDF, y no
dentro de la configuración de componentes:

| Opción | Qué hace |
| --- | --- |
| Tiempo de guarda corto | El de la tecla `1`, en milisegundos |
| Tiempo de guarda razonable | El de la tecla `2`, en milisegundos |
| Espera antes de confirmar el nivel | El de la tecla `v`: algo mayor que el rebote y menor que la guarda |
| Longitud de la cola de eventos | Cuántos eventos caben entre la rutina de interrupción y la tarea |
| ANTIPATRON: trazar dentro de la rutina de interrupcion | Ver el apartado siguiente. Desactivada |

El proyecto fija el tic del sistema operativo a 1000 Hz en
`sdkconfig.defaults`, como todos los de la asignatura: es lo que permite
sondear cada milisegundo. Si el programa avisa al arrancar de que el tic no
está a 1000 Hz, tu `sdkconfig` trae otro valor, y `sdkconfig.defaults` no
cambia las opciones que ya están en él: borra el fichero `sdkconfig` y vuelve
a fijar el destino.

## El antipatrón: trazar dentro de la interrupción

La última opción del menú añade una traza, `ESP_LOGI()`, dentro de las dos
rutinas de interrupción. Está desactivada, y está aquí **solo para enseñar lo
que no se hace**: la documentación de ESP-IDF dice que las macros `ESP_LOGx` no
deben usarse en una rutina de interrupción, y la regla de la sesión es que la
rutina de interrupción anota y entrega, nada más.

Con la versión de ESP-IDF del curso, lo esperable es que la placa se detenga
en la primera traza, con un mensaje de `abort()`, y se reinicie: para escribir
en la consola hay que tomar un cerrojo, y desde una rutina de interrupción no
se puede esperar a nadie. Que una traza dentro de una interrupción no falle en
otro programa no quiere decir que sea correcta. No lo repitas en ningún
proyecto, y desactiva la opción después de verlo.

## Estructura

```
CMakeLists.txt                define el proyecto
sdkconfig.defaults            configuración de partida; se escribe a mano y se versiona
main/
  CMakeLists.txt              registra las fuentes del componente principal
  Kconfig.projbuild           las opciones de la tabla de arriba
  pines.h                     los pines y su montaje, en un solo sitio
  entrada_por_interrupcion.c  el programa: las rutinas de interrupción, las tareas y las teclas
  filtro.c, filtro.h          el antirrebote: tiempo de guarda y confirmación del nivel
  rafaga.c, rafaga.h          la anotación de los flancos de una ráfaga para trazarla
components/
  led/                        componente propio: el LED, el mismo de U2-S1
  pulsador/                   componente propio: el pulsador
```

El programa tiene tres tareas: una atiende el pin, otra escribe las trazas y
otra lee las teclas. La que atiende el pin no escribe nada en la consola,
porque escribir es lento y la retrasaría: anota los flancos y, cuando termina
la ráfaga, se los pasa a la que escribe, que tiene menos prioridad. Las tareas
y sus prioridades se estudian en la U3, y las colas entre ellas, en la U4.

Los componentes `led` y `pulsador` no saben en qué pin están: se lo dice
`main` al iniciarlos. El de `pulsador` configura el pin, pone o quita la
resistencia y lee el nivel; la rutina de interrupción, la cola, la
notificación y el antirrebote están en `main`, que es donde se estudian.

Hay dos cosas que **no** se versionan y que por eso no están aquí: el fichero
`sdkconfig`, que genera la herramienta a partir de `sdkconfig.defaults`, y la
carpeta `build/`.

## Un aviso

Este repositorio es material de la asignatura. El repositorio de tu **proyecto
final** es otro distinto, con su propio nombre y sus propias reglas, y llega más
adelante en el curso.
