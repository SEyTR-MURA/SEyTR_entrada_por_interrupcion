# CMakeLists.txt de componente. Registra las fuentes y las cabeceras publicas.
# Si anades un fichero .c, anadelo tambien a SRCS.
# PRIV_REQUIRES dice de que componentes depende este: el LED y el pulsador del
# propio proyecto, y esp_timer, que da el reloj del sistema en microsegundos
# con el que se toman las marcas de tiempo.
idf_component_register(SRCS "entrada_por_interrupcion.c" "filtro.c" "rafaga.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES led pulsador esp_timer)
