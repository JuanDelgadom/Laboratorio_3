/*
 * connections.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * PROPOSITO:
 *   Define el estado compartido de la simulacion TCP: la tabla global
 *   de conexiones, el contador y el mutex de la tabla.
 *
 *   Al separar estas variables en su propio archivo de compilacion,
 *   broker_tcp.c, publisher_tcp.c y subscriber_tcp.c pueden referenciarlas
 *   con 'extern' (declarado en tcp_sim.h) y enlazarse juntos en un solo
 *   binario sin conflictos de simbolos.
 *
 *   NOTA: Esta simulacion usa hilos C11 y memoria compartida en proceso.
 *   Los tres roles (broker / publisher / subscriber) deben compilarse y
 *   enlazarse juntos. Para procesos separados se necesitaria IPC (sockets
 *   reales o memoria compartida POSIX) — eso es lo que implementan los
 *   archivos .s con llamadas al sistema socket()/connect()/bind().
 *
 * COMPILACION (todo en un solo binario):
 *   gcc -std=c11 -Wall -o sim \
 *       connections.c broker_tcp.c publisher_tcp.c subscriber_tcp.c \
 *       -lpthread
 */

#include "tcp_sim.h"

/* ============================================================
 * TABLA GLOBAL DE CONEXIONES
 * Definicion unica: todos los demas archivos la ven como 'extern'
 * gracias a la declaracion en tcp_sim.h.
 * ============================================================ */
tcp_conn_t connections[TCP_MAX_CONNS];
int        conn_count = 0;
mtx_t      conn_table_mutex;
 
/* ============================================================
 * PUNTO DE ENTRADA UNICO
 * Lanza broker, publisher y subscriber como hilos C11.
 * Uso: ./sim "<partido>"
 * Ejemplo: ./sim "Colombia vs Brasil"
 * ============================================================ */
int main(void) {
    /* Hilo del broker (corre en bucle continuo) */
    thrd_t t_broker;
    thrd_create(&t_broker, broker_run, NULL);
 
    /* Pausa para que el broker inicialice todas las conexiones */
    struct timespec ts = {0, 100000000L}; /* 100 ms */
    thrd_sleep(&ts, NULL);
 
    /*
     * Lanzar los 2 suscriptores ANTES que los publishers para que
     * esten conectados y listos al momento en que lleguen los mensajes.
     * conn=2 → suscriptor 1,  conn=3 → suscriptor 2.
     * duration_sec=5: activos durante 5 s (suficiente para recibir
     * todos los mensajes de ambos publishers).
     */
    sub_args_t sub1_args = { .conn_id = 2, .duration_sec = 5 };
    sub_args_t sub2_args = { .conn_id = 3, .duration_sec = 5 };
    thrd_t t_sub1, t_sub2;
    thrd_create(&t_sub1, subscriber_run, &sub1_args);
    thrd_create(&t_sub2, subscriber_run, &sub2_args);
 
    /* Pausa para que los suscriptores completen el handshake */
    thrd_sleep(&ts, NULL);
 
    /*
     * Lanzar los 2 publicadores.
     * conn=0 → partido 1,  conn=1 → partido 2.
     * Cada uno enviara 12 mensajes automaticamente.
     */
    pub_args_t pub1_args = { .conn_id = 0, .partido = "Colombia vs Brasil" };
    pub_args_t pub2_args = { .conn_id = 1, .partido = "Argentina vs Uruguay" };
    thrd_t t_pub1, t_pub2;
    thrd_create(&t_pub1, publisher_run, &pub1_args);
    thrd_create(&t_pub2, publisher_run, &pub2_args);
 
    /* Esperar que los publishers terminen de enviar sus mensajes */
    thrd_join(t_pub1, NULL);
    thrd_join(t_pub2, NULL);
 
    /* Esperar que los suscriptores terminen (timeout interno) */
    thrd_join(t_sub1, NULL);
    thrd_join(t_sub2, NULL);
 
    printf("\n[SIM] Simulacion completada.\n");
    return EXIT_SUCCESS;
}
