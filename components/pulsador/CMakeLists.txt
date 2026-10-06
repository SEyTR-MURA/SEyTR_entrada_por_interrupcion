# REQUIRES lleva los componentes cuyas cabeceras aparecen en la cabecera
# publica de este: pulsador.h incluye driver/gpio.h, que es de esp_driver_gpio.
idf_component_register(SRCS "pulsador.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio)
