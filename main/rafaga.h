/* rafaga.h - anotación de los flancos de una ráfaga para trazarlos después.
 *
 * Trazar cada flanco en el momento en que llega retrasaría a la tarea que los
 * atiende: la consola escribe unos 11,5 caracteres por milisegundo. Por eso
 * los flancos se anotan aquí, con su instante relativo al primero, y la ráfaga
 * se traza entera cuando termina, desde otra tarea de menos prioridad.
 *
 * No depende de ESP-IDF ni de FreeRTOS.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Cuántos flancos de una ráfaga se guardan con su instante. Los que pasen de
 * aquí se cuentan, pero no se guardan. */
#define RAFAGA_MAX_FLANCOS  48

typedef struct {
    uint32_t dt_us;   /* microsegundos desde el primer flanco de la ráfaga */
    char     marca;   /* lo que decidió el filtro: '*', '.' o 'x' */
    uint8_t  avisos;  /* con notificación, cuántos avisos había acumulados; si no, 1 */
} flanco_t;

typedef struct {
    bool     abierta;      /* hay al menos un flanco anotado */
    int64_t  inicio_us;    /* instante del primer flanco */
    int64_t  ultimo_us;    /* instante del último flanco */
    uint32_t flancos;      /* flancos anotados, guardados o no */
    uint32_t pulsaciones;  /* de ellos, con MARCA_PULSACION */
    uint32_t rebotes;      /* con MARCA_REBOTE */
    uint32_t rechazados;   /* con MARCA_RECHAZADA */
    uint16_t guardados;    /* cuántos hay en f[] */
    flanco_t f[RAFAGA_MAX_FLANCOS];
} rafaga_t;

/* Deja la ráfaga vacía y cerrada. */
void rafaga_vaciar(rafaga_t *rafaga);

/* Anota un flanco. El primero abre la ráfaga. */
void rafaga_anotar(rafaga_t *rafaga, int64_t us, char marca, uint32_t avisos);

/* La ráfaga ha terminado si está abierta y han pasado silencio_us sin flancos,
 * o si dura ya maximo_us: lo segundo evita que un pin que no para de cambiar
 * deje la ráfaga abierta para siempre. */
bool rafaga_terminada(const rafaga_t *rafaga, int64_t ahora_us,
                      int64_t silencio_us, int64_t maximo_us);
