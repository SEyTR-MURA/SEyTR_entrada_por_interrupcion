/* entrada_por_interrupcion.c - SEyTR, Unidad 2.
 *
 * Qué hace este programa
 * ----------------------
 * Cuenta las pulsaciones de un pulsador y traza cada ráfaga de flancos con el
 * instante de cada uno, en microsegundos. Así se ve el rebote: una sola
 * pulsación produce varios flancos en unos pocos milisegundos.
 *
 * Desde el monitor, sin recompilar, se elige cómo se entera el programa de que
 * el pin ha cambiado —leyéndolo periódicamente, por sondeo, o con una
 * interrupción que entrega el evento a una tarea por una cola o por una
 * notificación— y cómo filtra el rebote: con un tiempo de guarda y con la
 * confirmación del nivel. Las teclas se listan al arrancar y con «h».
 *
 * Qué hardware necesita
 * ---------------------
 * La placa del curso, conectada por su conector USB-a-UART: es por donde
 * llegan las teclas. Un pulsador entre un pin y masa, o un cable que se toca
 * contra masa; su pin se declara en pines.h. El LED es opcional.
 *
 * Las tres tareas
 * ---------------
 * - «entrada» atiende el pin: lo lee, o recibe los eventos de la rutina de
 *   interrupción, filtra el rebote y cuenta. Es la de más prioridad.
 * - «impresora» escribe las trazas. Tiene la prioridad más baja: escribir en
 *   la consola es lento, y no debe retrasar a la que atiende el pin. Por eso
 *   «entrada» no traza nada: le manda a «impresora» lo que hay que escribir.
 * - «teclado» lee las teclas y le manda a «entrada» los ajustes nuevos.
 * Las tareas, sus prioridades y las colas entre ellas se estudian en la U3 y
 * en la U4; aquí basta con saber qué hace cada una.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "filtro.h"
#include "led.h"
#include "pines.h"
#include "pulsador.h"
#include "rafaga.h"

static const char *TAG = "entrada";

/* Valores de la configuración del proyecto. */
#define GUARDA_CORTA_MS     CONFIG_SEYTR_GUARDA_CORTA_MS
#define GUARDA_MS           CONFIG_SEYTR_GUARDA_MS
#define CONFIRMACION_MS     CONFIG_SEYTR_CONFIRMACION_MS
#define LONGITUD_COLA       CONFIG_SEYTR_LONGITUD_COLA

/* Cada cuánto resume la lectura del nivel lo que ha visto. */
#define LECTURA_RESUMEN_MS  500

/* Una ráfaga se da por terminada, y se traza, cuando pasan 100 ms sin flancos
 * o cuando dura ya un segundo. Solo sirve para agrupar lo que se escribe: no
 * interviene en el antirrebote. */
#define RAFAGA_SILENCIO_MS  100
#define RAFAGA_MAXIMO_MS    1000

/* Con interrupción, cuánto espera la tarea un evento antes de mirar si ha
 * terminado la ráfaga o han llegado ajustes nuevos. */
#define ESPERA_EVENTO_MS    20

/* Cuántos mensajes caben entre «entrada» e «impresora». */
#define LONGITUD_MENSAJES   4

/* Cómo se entera el programa de que el pin ha cambiado. */
typedef enum {
    MODO_LECTURA,       /* lee el nivel cada milisegundo y lo resume */
    MODO_SONDEO,        /* lee el pin periódicamente y cuenta las transiciones */
    MODO_COLA,          /* interrupción que manda el evento por una cola */
    MODO_NOTIFICACION,  /* interrupción que notifica a la tarea */
} modo_t;

typedef enum {
    GUARDA_NINGUNA,
    GUARDA_CORTA,
    GUARDA_RAZONABLE,
} guarda_t;

typedef struct {
    modo_t   modo;
    uint32_t periodo_sondeo_ms;  /* 1 o 10 */
    guarda_t guarda;             /* solo con interrupción */
    bool     confirmar;          /* solo con interrupción */
    bool     polarizar;          /* resistencia de polarización interna */
} ajustes_t;

/* Ajustes con los que arranca el programa: el primer paso de la demostración
 * mira el nivel del pin. */
static const ajustes_t AJUSTES_INICIALES = {
    .modo = MODO_LECTURA,
    .periodo_sondeo_ms = 1,
    .guarda = GUARDA_NINGUNA,
    .confirmar = false,
    .polarizar = true,
};

static const uint32_t GUARDA_EN_MS[] = {
    [GUARDA_NINGUNA]   = 0,
    [GUARDA_CORTA]     = GUARDA_CORTA_MS,
    [GUARDA_RAZONABLE] = GUARDA_MS,
};

/* Lo que la rutina de interrupción manda por la cola: qué pin y en qué
 * instante. Es el evento de la diapositiva 15. */
typedef struct {
    uint32_t pin;
    int64_t  us;
} evento_t;

/* Lo que «entrada» le manda a «impresora». */
typedef enum {
    MENSAJE_AJUSTES,  /* han cambiado los ajustes y la cuenta empieza de cero */
    MENSAJE_RAFAGA,   /* ha terminado una ráfaga */
    MENSAJE_LECTURA,  /* resumen de la lectura del nivel */
} tipo_mensaje_t;

typedef struct {
    tipo_mensaje_t tipo;
    ajustes_t ajustes;
    uint32_t  total;     /* pulsaciones contadas desde el último cambio */
    uint32_t  fallos;    /* eventos que no cupieron en la cola */
    uint32_t  perdidos;  /* mensajes que no cupieron en la cola de la impresora */
    rafaga_t  rafaga;    /* MENSAJE_RAFAGA */
    int       nivel;     /* MENSAJE_LECTURA: nivel en la última lectura */
    uint32_t  muestras;  /* MENSAJE_LECTURA: lecturas del resumen */
    uint32_t  cambios;   /* MENSAJE_LECTURA: cambios de nivel entre lecturas */
    uint32_t  pulsadas;  /* MENSAJE_LECTURA: lecturas a nivel de pulsado */
} mensaje_t;

/* Lo que lleva la tarea «entrada». Solo la toca ella. */
typedef struct {
    ajustes_t  ajustes;
    filtro_t   filtro;
    rafaga_t   rafaga;
    uint32_t   total;
    uint32_t   perdidos;
    bool       isr_conectada;
    TickType_t referencia;  /* de xTaskDelayUntil(), en lectura y sondeo */
    int        anterior;    /* nivel en la lectura anterior */
    uint32_t   muestras;
    uint32_t   cambios;
    uint32_t   pulsadas;
} estado_t;

/* La cola de eventos se crea al arrancar, antes de instalar la interrupción,
 * con su longitud y el tamaño del elemento. */
static QueueHandle_t s_cola;

/* La tarea a la que notifica la rutina de interrupción. */
static TaskHandle_t s_tarea;

/* Eventos que no cupieron en la cola. Solo lo escribe la rutina de
 * interrupción, y la tarea lo lee para trazarlo. Es volatile porque cambia
 * fuera del flujo de la tarea: sin volatile, el compilador podría leerlo una
 * vez y no volver a hacerlo. */
static volatile uint32_t s_fallos;

static QueueHandle_t s_ajustes;   /* de «teclado» a «entrada»: un elemento */
static QueueHandle_t s_mensajes;  /* de «entrada» a «impresora» */

static pulsador_t s_pulsador;
static led_t s_led;
static estado_t s_estado;

/* ------------------------------------------------------------------------
 * Las dos rutinas de interrupción. Anotan y entregan: nada más.
 * ------------------------------------------------------------------------ */

/* Patrón A, diapositiva 15: el evento, con su instante, va por la cola. */
static void IRAM_ATTR isr_cola(void *arg)
{
    evento_t e = { .pin = (uint32_t)arg,
                   .us = esp_timer_get_time() };
    BaseType_t hp = pdFALSE;
    if (!xQueueSendFromISR(s_cola, &e, &hp)) s_fallos++;
#if CONFIG_SEYTR_TRAZA_EN_ISR
    /* ANTIPATRÓN. Está aquí solo para enseñarlo, y solo se compila si se
     * activa en la configuración. No se replica. */
    ESP_LOGI(TAG, "ISR: flanco en el GPIO %" PRIu32, e.pin);
#endif
    if (hp) portYIELD_FROM_ISR();
}

/* Patrón B, diapositiva 16: solo se avisa a la tarea. El aviso no lleva el
 * instante ni el pin, y si llegan varios antes de que la tarea despierte, se
 * acumulan en un número. */
static void IRAM_ATTR isr_notificacion(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_tarea, &hp);
#if CONFIG_SEYTR_TRAZA_EN_ISR
    /* ANTIPATRÓN, como en isr_cola(). */
    ESP_LOGI(TAG, "ISR: aviso a la tarea");
#endif
    if (hp) portYIELD_FROM_ISR();
}

/* ------------------------------------------------------------------------
 * La tarea «entrada».
 * ------------------------------------------------------------------------ */

/* La confirmación del nivel de la diapositiva 20: espera a que llegue el
 * instante pedido y lee el pin. vTaskDelay(n) cuenta desde el tic en curso, ya
 * empezado, así que puede esperar hasta casi un tic menos de lo pedido: por
 * eso se le suma uno. Mientras espera, la rutina de interrupción sigue
 * atendiendo flancos: con cola, sus eventos esperan en ella; con
 * notificación, los avisos se acumulan en la cuenta de la notificación.
 * Con un tic de 1 ms, cada milisegundo es un tic; con otro tic, la espera
 * puede quedarse corta, y el programa avisa al arrancar. */
static bool sigue_pulsado(int64_t instante_us, void *contexto)
{
    const pulsador_t *pulsador = contexto;
    const int64_t falta_us = instante_us - esp_timer_get_time();
    if (falta_us > 0) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)((falta_us + 999) / 1000)) + 1);
    }
    return pulsador_pulsado(pulsador);
}

/* Manda un mensaje a «impresora» sin esperar: si su cola está llena, el
 * mensaje se pierde y se cuenta, pero «entrada» no se detiene nunca por la
 * consola. */
static void enviar(estado_t *s, tipo_mensaje_t tipo)
{
    static mensaje_t m;  /* estático: es grande para la pila de la tarea */
    m.tipo = tipo;
    m.ajustes = s->ajustes;
    m.total = s->total;
    m.fallos = s_fallos;
    m.perdidos = s->perdidos;
    if (tipo == MENSAJE_RAFAGA) {
        m.rafaga = s->rafaga;
    }
    if (tipo == MENSAJE_LECTURA) {
        m.nivel = s->anterior;
        m.muestras = s->muestras;
        m.cambios = s->cambios;
        m.pulsadas = s->pulsadas;
    }
    if (xQueueSend(s_mensajes, &m, 0) != pdTRUE) {
        s->perdidos++;
    }
}

static void cerrar_rafaga(estado_t *s)
{
    if (s->rafaga.abierta) {
        enviar(s, MENSAJE_RAFAGA);
    }
    rafaga_vaciar(&s->rafaga);
}

static void cerrar_si_terminada(estado_t *s)
{
    if (rafaga_terminada(&s->rafaga, esp_timer_get_time(),
                         (int64_t)RAFAGA_SILENCIO_MS * 1000,
                         (int64_t)RAFAGA_MAXIMO_MS * 1000)) {
        cerrar_rafaga(s);
    }
}

/* Anota el flanco en la ráfaga y, si es una pulsación, la cuenta. */
static void anotar(estado_t *s, int64_t us, char marca, uint32_t avisos)
{
    rafaga_anotar(&s->rafaga, us, marca, avisos);
    if (marca == MARCA_PULSACION) {
        s->total++;
        led_conmutar(&s_led);
    }
}

/* Aplica unos ajustes nuevos. La cuenta empieza siempre de cero. */
static void aplicar(estado_t *s, const ajustes_t *nuevos)
{
    cerrar_rafaga(s);

    /* Primero se desconecta la rutina de interrupción, si la había, para
     * cambiar el resto sin que lleguen eventos a medias. */
    if (s->isr_conectada) {
        ESP_ERROR_CHECK(gpio_isr_handler_remove(PIN_PULSADOR));
        s->isr_conectada = false;
    }

    s->ajustes = *nuevos;
    ESP_ERROR_CHECK(pulsador_polarizar(&s_pulsador, nuevos->polarizar));

    /* Se añade un manejador por pin, con un argumento propio: diapositiva 12.
     * El tipo de disparo, el flanco hacia el nivel de pulsado, lo fijó el
     * componente del pulsador al configurar el pin. */
    if (nuevos->modo == MODO_COLA) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_PULSADOR, isr_cola, (void *)PIN_PULSADOR));
        s->isr_conectada = true;
    } else if (nuevos->modo == MODO_NOTIFICACION) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_PULSADOR, isr_notificacion, NULL));
        s->isr_conectada = true;
    }

    /* Se deja que el pin se asiente tras el cambio de resistencia, y se tira
     * lo que haya podido llegar mientras tanto, también un flanco que se
     * hubiera quedado anotado de antes. */
    vTaskDelay(pdMS_TO_TICKS(10));
    xQueueReset(s_cola);
    ulTaskNotifyTake(pdTRUE, 0);
    s_fallos = 0;

    s->filtro.guarda_us = (int64_t)GUARDA_EN_MS[nuevos->guarda] * 1000;
    s->filtro.confirmar = nuevos->confirmar;
    s->filtro.confirmacion_us = (int64_t)CONFIRMACION_MS * 1000;
    filtro_reiniciar(&s->filtro);

    s->total = 0;
    s->perdidos = 0;
    s->anterior = pulsador_nivel(&s_pulsador);
    s->muestras = 0;
    s->cambios = 0;
    s->pulsadas = 0;

    enviar(s, MENSAJE_AJUSTES);

    /* La referencia de xTaskDelayUntil() se ancla aquí. Dentro del bucle no
     * se toca: la actualiza la propia función. */
    s->referencia = xTaskGetTickCount();
}

/* Lectura del nivel: cada milisegundo, y un resumen cada medio segundo. Sirve
 * para ver el pin flotante: sin resistencia de polarización, la lectura no
 * está determinada. */
static void paso_lectura(estado_t *s)
{
    const int nivel = pulsador_nivel(&s_pulsador);
    if (nivel != s->anterior) {
        s->cambios++;
    }
    if (nivel == s_pulsador.nivel_pulsado) {
        s->pulsadas++;
    }
    s->anterior = nivel;
    s->muestras++;

    if (s->muestras >= LECTURA_RESUMEN_MS) {
        enviar(s, MENSAJE_LECTURA);
        s->muestras = 0;
        s->cambios = 0;
        s->pulsadas = 0;
    }
    xTaskDelayUntil(&s->referencia, pdMS_TO_TICKS(1));
}

/* Sondeo, diapositiva 7: se lee el pin con un periodo estable, esperando
 * hasta instante absoluto, y se cuenta cada transición al nivel de pulsado.
 * Sin ningún filtro. */
static void paso_sondeo(estado_t *s)
{
    const int nivel = pulsador_nivel(&s_pulsador);
    if (nivel != s->anterior && nivel == s_pulsador.nivel_pulsado) {
        anotar(s, esp_timer_get_time(), MARCA_PULSACION, 1);
    }
    s->anterior = nivel;
    cerrar_si_terminada(s);
    xTaskDelayUntil(&s->referencia, pdMS_TO_TICKS(s->ajustes.periodo_sondeo_ms));
}

/* Interrupción con cola: la tarea recibe el evento, que trae su instante, y
 * filtra el rebote con él. Es el bucle de la diapositiva 19, con el filtro en
 * filtro.c. */
static void paso_cola(estado_t *s)
{
    evento_t e;
    if (xQueueReceive(s_cola, &e, pdMS_TO_TICKS(ESPERA_EVENTO_MS)) == pdTRUE) {
        const char marca = filtro_flanco(&s->filtro, e.us, sigue_pulsado, &s_pulsador);
        anotar(s, e.us, marca, 1);
    }
    cerrar_si_terminada(s);
}

/* Interrupción con notificación: la tarea solo sabe que ha ocurrido algo, y
 * cuántas veces desde que la tomó por última vez. El instante lo toma ella al
 * despertar: es lo único que tiene. */
static void paso_notificacion(estado_t *s)
{
    const uint32_t avisos = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ESPERA_EVENTO_MS));
    if (avisos > 0) {
        const int64_t us = esp_timer_get_time();
        const char marca = filtro_flanco(&s->filtro, us, sigue_pulsado, &s_pulsador);
        anotar(s, us, marca, avisos);
    }
    cerrar_si_terminada(s);
}

static void tarea_entrada(void *argumento)
{
    (void)argumento;
    estado_t *s = &s_estado;

    /* La rutina de notificación avisa a esta tarea. */
    s_tarea = xTaskGetCurrentTaskHandle();

    ajustes_t nuevos = AJUSTES_INICIALES;
    aplicar(s, &nuevos);

    for (;;) {
        if (xQueueReceive(s_ajustes, &nuevos, 0) == pdTRUE) {
            aplicar(s, &nuevos);
        }
        switch (s->ajustes.modo) {
        case MODO_LECTURA:      paso_lectura(s);      break;
        case MODO_SONDEO:       paso_sondeo(s);       break;
        case MODO_COLA:         paso_cola(s);         break;
        case MODO_NOTIFICACION: paso_notificacion(s); break;
        default:                vTaskDelay(1);        break;
        }
    }
}

/* ------------------------------------------------------------------------
 * La tarea «impresora».
 * ------------------------------------------------------------------------ */

/* Escribe en texto una duración dada en microsegundos, como milisegundos con
 * tres decimales. Se hace con enteros para no depender del formato de coma
 * flotante de la biblioteca estándar. */
static const char *en_ms(int64_t us, char *texto, size_t tamano)
{
    if (us < 0) {
        us = 0;
    }
    snprintf(texto, tamano, "%lld.%03lld", (long long)(us / 1000), (long long)(us % 1000));
    return texto;
}

static const char *nombre_resistencia(void)
{
    return (s_pulsador.nivel_pulsado == 0) ? "pull-up" : "pull-down";
}

static void imprimir_ajustes(const mensaje_t *m)
{
    const ajustes_t *a = &m->ajustes;
    const char *polarizacion = a->polarizar ? "activado" : "DESACTIVADO: sin resistencia externa, el pin queda flotante";

    switch (a->modo) {
    case MODO_LECTURA:
        ESP_LOGI(TAG, "--- lectura del nivel cada 1 ms, resumen cada %d ms; %s interno: %s",
                 LECTURA_RESUMEN_MS, nombre_resistencia(), polarizacion);
        break;
    case MODO_SONDEO:
        ESP_LOGI(TAG, "--- sondeo cada %" PRIu32 " ms, sin filtro; %s interno: %s",
                 a->periodo_sondeo_ms, nombre_resistencia(), polarizacion);
        break;
    case MODO_COLA:
    case MODO_NOTIFICACION:
        ESP_LOGI(TAG, "--- interrupcion con %s; tiempo de guarda: %" PRIu32 " ms; "
                      "confirmacion del nivel: %s; %s interno: %s",
                 (a->modo == MODO_COLA) ? "cola" : "notificacion directa",
                 GUARDA_EN_MS[a->guarda],
                 a->confirmar ? "si" : "no",
                 nombre_resistencia(), polarizacion);
        break;
    default:
        break;
    }
    ESP_LOGI(TAG, "--- la cuenta empieza de cero");
}

static void imprimir_rafaga(const mensaje_t *m)
{
    const rafaga_t *r = &m->rafaga;
    const bool notificacion = (m->ajustes.modo == MODO_NOTIFICACION);
    char duracion[24];
    en_ms(r->ultimo_us - r->inicio_us, duracion, sizeof duracion);

    if (m->ajustes.modo == MODO_SONDEO) {
        ESP_LOGI(TAG, "rafaga: %" PRIu32 " transiciones a pulsado en %s ms",
                 r->flancos, duracion);
    } else {
        ESP_LOGI(TAG, "rafaga: %" PRIu32 " %s en %s ms; contadas (*): %" PRIu32 ", "
                      "rebotes (.): %" PRIu32 ", sin confirmar (x): %" PRIu32,
                 r->flancos, notificacion ? "despertares" : "flancos", duracion,
                 r->pulsaciones, r->rebotes, r->rechazados);
    }

    /* Los instantes, en microsegundos desde el primero, seis por línea. Con
     * notificación, entre paréntesis, cuántos avisos se habían acumulado
     * cuando la tarea despertó. */
    char linea[128];
    size_t largo = 0;
    for (uint16_t i = 0; i < r->guardados; i++) {
        const flanco_t *f = &r->f[i];
        char dt[16];
        snprintf(dt, sizeof dt, "+%" PRIu32, f->dt_us);
        int n;
        if (notificacion) {
            n = snprintf(linea + largo, sizeof linea - largo, " %9s %c(%u)",
                         dt, f->marca, (unsigned)f->avisos);
        } else {
            n = snprintf(linea + largo, sizeof linea - largo, " %9s %c", dt, f->marca);
        }
        if (n > 0) {
            largo += (size_t)n;
        }
        if ((i % 6) == 5 || i + 1 == r->guardados || largo >= sizeof linea - 24) {
            printf("   %s\n", linea);
            largo = 0;
            linea[0] = '\0';
        }
    }
    if (r->flancos > r->guardados) {
        printf("   (solo se guardan los %d primeros)\n", RAFAGA_MAX_FLANCOS);
    }

    if (m->ajustes.modo == MODO_COLA) {
        ESP_LOGI(TAG, "pulsaciones en total: %" PRIu32 "; eventos perdidos por cola llena: %" PRIu32,
                 m->total, m->fallos);
    } else {
        ESP_LOGI(TAG, "pulsaciones en total: %" PRIu32, m->total);
    }
}

static void imprimir_lectura(const mensaje_t *m)
{
    ESP_LOGI(TAG, "nivel %d; en %" PRIu32 " lecturas: %" PRIu32 " cambios, %" PRIu32
                  " a nivel de pulsado; %s interno %s",
             m->nivel, m->muestras, m->cambios, m->pulsadas,
             nombre_resistencia(), m->ajustes.polarizar ? "activado" : "DESACTIVADO");
}

static void tarea_impresora(void *argumento)
{
    (void)argumento;
    static mensaje_t m;
    uint32_t perdidos_avisados = 0;

    for (;;) {
        xQueueReceive(s_mensajes, &m, portMAX_DELAY);
        /* Si se perdió el mensaje del último cambio de ajustes, la cuenta de
         * perdidos ha vuelto a cero sin que esta tarea se enterase. */
        if (m.perdidos < perdidos_avisados) {
            perdidos_avisados = 0;
        }
        switch (m.tipo) {
        case MENSAJE_AJUSTES: imprimir_ajustes(&m); perdidos_avisados = 0; break;
        case MENSAJE_RAFAGA:  imprimir_rafaga(&m);  break;
        case MENSAJE_LECTURA: imprimir_lectura(&m); break;
        default:                                    break;
        }
        if (m.perdidos > perdidos_avisados) {
            ESP_LOGW(TAG, "la consola no da abasto: %" PRIu32 " mensajes sin escribir",
                     m.perdidos);
            perdidos_avisados = m.perdidos;
        }
    }
}

/* ------------------------------------------------------------------------
 * La tarea «teclado».
 * ------------------------------------------------------------------------ */

static void mostrar_ayuda(void)
{
    printf("\n"
           "Teclas (con el monitor abierto, sin pulsar Intro):\n"
           "  l  lectura del nivel cada 1 ms, con un resumen cada %d ms\n"
           "  s  sondeo, sin filtro\n"
           "  t  periodo del sondeo: 1 ms o 10 ms\n"
           "  c  interrupcion con cola\n"
           "  n  interrupcion con notificacion directa\n"
           "  0  sin tiempo de guarda\n"
           "  1  tiempo de guarda corto, %d ms\n"
           "  2  tiempo de guarda razonable, %d ms\n"
           "  v  confirmacion del nivel %d ms despues del flanco: si o no\n"
           "  p  resistencia de polarizacion interna: activada o desactivada\n"
           "  z  la cuenta vuelve a empezar\n"
           "  h  esta ayuda\n"
           "La guarda y la confirmacion solo actuan con interrupcion.\n"
           "Cada tecla de variante, y la z, hace que la cuenta empiece de cero.\n\n",
           LECTURA_RESUMEN_MS, GUARDA_CORTA_MS, GUARDA_MS, CONFIRMACION_MS);
}

static void tarea_teclado(void *argumento)
{
    (void)argumento;
    ajustes_t ajustes = AJUSTES_INICIALES;

    for (;;) {
        /* Mientras no se instale el controlador de la UART, y aquí no se
         * instala, read() sobre la consola no se queda esperando: si no ha
         * llegado nada, devuelve -1 enseguida. Por eso se consulta cada
         * 20 ms. */
        char tecla;
        if (read(STDIN_FILENO, &tecla, 1) == 1) {
            bool enviar_ajustes = true;
            switch (tecla) {
            case 'l': ajustes.modo = MODO_LECTURA;                break;
            case 's': ajustes.modo = MODO_SONDEO;                 break;
            case 'c': ajustes.modo = MODO_COLA;                   break;
            case 'n': ajustes.modo = MODO_NOTIFICACION;           break;
            case 't': ajustes.periodo_sondeo_ms =
                          (ajustes.periodo_sondeo_ms == 1) ? 10 : 1; break;
            case '0': ajustes.guarda = GUARDA_NINGUNA;            break;
            case '1': ajustes.guarda = GUARDA_CORTA;              break;
            case '2': ajustes.guarda = GUARDA_RAZONABLE;          break;
            case 'v': ajustes.confirmar = !ajustes.confirmar;     break;
            case 'p': ajustes.polarizar = !ajustes.polarizar;     break;
            case 'z':                                             break;
            case 'h':
            case '?': mostrar_ayuda(); enviar_ajustes = false;    break;
            default:  enviar_ajustes = false;                     break;
            }
            if (enviar_ajustes) {
                xQueueOverwrite(s_ajustes, &ajustes);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ------------------------------------------------------------------------
 * Arranque.
 * ------------------------------------------------------------------------ */

void app_main(void)
{
    ESP_LOGI(TAG, "SEyTR - entrada por interrupcion. Tic a %d Hz", configTICK_RATE_HZ);

    if (configTICK_RATE_HZ != 1000) {
        ESP_LOGW(TAG, "el tic no esta a 1000 Hz: el sondeo no puede leer el pin cada milisegundo");
    }

    ESP_ERROR_CHECK(led_iniciar(&s_led, PIN_LED, LED_NIVEL_ENCENDIDO));

    if (PIN_PULSADOR == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "no hay pin para el pulsador: eligelo en main/pines.h. "
                      "Sin pulsador no hay nada que medir");
        return;
    }
    ESP_ERROR_CHECK(pulsador_iniciar(&s_pulsador, PIN_PULSADOR, PULSADOR_NIVEL_PULSADO));

    /* La cola, antes de instalar la interrupción: diapositiva 15. */
    s_cola = xQueueCreate(LONGITUD_COLA, sizeof(evento_t));
    s_ajustes = xQueueCreate(1, sizeof(ajustes_t));
    s_mensajes = xQueueCreate(LONGITUD_MENSAJES, sizeof(mensaje_t));
    if (s_cola == NULL || s_ajustes == NULL || s_mensajes == NULL) {
        ESP_LOGE(TAG, "no hay memoria para las colas");
        return;
    }

    /* El servicio de interrupciones de GPIO se instala una sola vez para todo
     * el proyecto: diapositiva 12. Con 0, sin ESP_INTR_FLAG_IRAM. */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    mostrar_ayuda();

    /* «entrada» tiene la prioridad más alta de las tres, para que ni la
     * consola ni el teclado retrasen la lectura del pin. */
    const BaseType_t impresora =
        xTaskCreate(tarea_impresora, "impresora", 4096, NULL, 3, NULL);
    const BaseType_t entrada =
        xTaskCreate(tarea_entrada, "entrada", 4096, NULL, 10, NULL);
    const BaseType_t teclado =
        xTaskCreate(tarea_teclado, "teclado", 3072, NULL, 5, NULL);
    if (impresora != pdPASS || entrada != pdPASS || teclado != pdPASS) {
        ESP_LOGE(TAG, "no hay memoria para las tareas");
        return;
    }

    /* app_main termina aquí; las tres tareas siguen. */
}
