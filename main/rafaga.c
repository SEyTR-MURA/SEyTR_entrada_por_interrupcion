/* rafaga.c - implementación de la anotación de ráfagas. */

#include "rafaga.h"

#include <string.h>

#include "filtro.h"

void rafaga_vaciar(rafaga_t *rafaga)
{
    memset(rafaga, 0, sizeof *rafaga);
}

void rafaga_anotar(rafaga_t *rafaga, int64_t us, char marca, uint32_t avisos)
{
    if (!rafaga->abierta) {
        rafaga->abierta = true;
        rafaga->inicio_us = us;
    }
    rafaga->ultimo_us = us;
    rafaga->flancos++;

    switch (marca) {
    case MARCA_PULSACION: rafaga->pulsaciones++; break;
    case MARCA_REBOTE:    rafaga->rebotes++;     break;
    case MARCA_RECHAZADA: rafaga->rechazados++;  break;
    default:                                     break;
    }

    if (rafaga->guardados < RAFAGA_MAX_FLANCOS) {
        int64_t dt = us - rafaga->inicio_us;
        if (dt < 0) {
            dt = 0;
        } else if (dt > UINT32_MAX) {
            dt = UINT32_MAX;
        }
        flanco_t *f = &rafaga->f[rafaga->guardados++];
        f->dt_us = (uint32_t)dt;
        f->marca = marca;
        f->avisos = (avisos > UINT8_MAX) ? UINT8_MAX : (uint8_t)avisos;
    }
}

bool rafaga_terminada(const rafaga_t *rafaga, int64_t ahora_us,
                      int64_t silencio_us, int64_t maximo_us)
{
    if (!rafaga->abierta) {
        return false;
    }
    return (ahora_us - rafaga->ultimo_us >= silencio_us)
        || (ahora_us - rafaga->inicio_us >= maximo_us);
}
