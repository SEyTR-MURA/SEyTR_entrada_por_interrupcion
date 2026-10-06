/* filtro.h - el antirrebote de la tarea: tiempo de guarda y confirmación del
 * nivel.
 *
 * Es el código de las diapositivas 19 y 20 de U2-S2, puesto en una función
 * para que lo usen igual las dos variantes con interrupción, la de cola y la
 * de notificación. No depende de ESP-IDF ni de FreeRTOS: quien lo usa le da el
 * instante de cada flanco y una función que dice si el pin sigue pulsado. Por
 * eso se puede probar también fuera de la placa.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Lo que el filtro decide de cada flanco. Son también las marcas con las que
 * el programa lo traza. */
#define MARCA_PULSACION  '*'  /* pulsación nueva: se cuenta */
#define MARCA_REBOTE     '.'  /* dentro del tiempo de guarda: se descarta */
#define MARCA_RECHAZADA  'x'  /* fuera de la guarda, pero el nivel no se confirma */

typedef struct {
    int64_t guarda_us;        /* tiempo de guarda; 0, sin guarda */
    bool    confirmar;        /* si se confirma el nivel pasado el rebote */
    int64_t confirmacion_us;  /* cuánto después del flanco se confirma */
    int64_t ultimo_us;        /* instante del último flanco fuera de la guarda, contado o rechazado */
} filtro_t;

/* Dice si el pin está a nivel de pulsado en el instante instante_us, que es
 * posterior al flanco. La de la placa espera a que llegue ese instante y lee
 * el pin. contexto es lo que se pasó a filtro_flanco(). */
typedef bool (*sigue_pulsado_t)(int64_t instante_us, void *contexto);

/* Olvida el último flanco: el siguiente queda siempre fuera de la guarda. */
void filtro_reiniciar(filtro_t *filtro);

/* Decide qué es el flanco que llegó en el instante us, en microsegundos.
 * Devuelve MARCA_PULSACION, MARCA_REBOTE o MARCA_RECHAZADA. */
char filtro_flanco(filtro_t *filtro, int64_t us,
                   sigue_pulsado_t sigue_pulsado, void *contexto);
