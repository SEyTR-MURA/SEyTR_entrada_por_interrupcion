/* filtro.c - implementación del antirrebote de la tarea. */

#include "filtro.h"

void filtro_reiniciar(filtro_t *filtro)
{
    /* Un instante muy en el pasado, para que el primer flanco quede siempre
     * fuera del tiempo de guarda. No es el mínimo de int64_t porque la resta
     * de filtro_flanco() se saldría del tipo. */
    filtro->ultimo_us = INT64_MIN / 2;
}

char filtro_flanco(filtro_t *filtro, int64_t us,
                   sigue_pulsado_t sigue_pulsado, void *contexto)
{
    /* rebote: llega dentro del tiempo de guarda */
    if (us - filtro->ultimo_us < filtro->guarda_us) {
        return MARCA_REBOTE;
    }
    /* El instante se anota aunque la confirmación rechace el flanco: así el
     * tiempo de guarda empieza a contar en él, y los flancos que siguen en la
     * misma ráfaga se descartan como rebote, en lugar de pasar uno a uno por
     * la confirmación. */
    filtro->ultimo_us = us;

    /* Confirmación del nivel: pasado el rebote, el pin tiene que seguir
     * pulsado. Es lo que distingue una pulsación de la ráfaga que produce
     * el contacto al soltarse. */
    if (filtro->confirmar && !sigue_pulsado(us + filtro->confirmacion_us, contexto)) {
        return MARCA_RECHAZADA;
    }
    return MARCA_PULSACION;
}
