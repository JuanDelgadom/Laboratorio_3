/*
 * subscriber_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Suscriptor (hincha que sigue el partido en tiempo real).
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>    - printf(), fprintf(): mostrar mensajes recibidos y errores.
 *   <stdlib.h>   - exit(), rand(), srand(), atoi(): control y conversion.
 *   <string.h>   - strlen(), memcpy(): manejo de payloads de segmentos TCP.
 *   <stdint.h>   - uint32_t, uint16_t, uint8_t: campos del header TCP simulado.
 *   <stdbool.h>  - bool, true, false: control de estado de la conexion.
 *   <time.h>     - time(), thrd_sleep(): ISN aleatorio y retardos simulados.
 *   <threads.h>  - thrd_t, thrd_create, thrd_join: hilo de recepcion de datos.
 *   "tcp_sim.h"  - Protocolo TCP simulado: segmentos, estados FSM, colas.
 *
 * SIMULACION TCP:
 *   - Ejecuta el handshake de 3 vias como cliente (igual que el publisher).
 *   - Recibe segmentos FLAG_PSH|FLAG_ACK con los mensajes del broker.
 *   - Envia ACK por cada segmento recibido (confirmacion de entrega).
 *   - El orden de recepcion esta garantizado por la cola FIFO (equivale
 *     al reordenamiento de segmentos TCP en el receptor real).
 *   - Cierre ordenado con FIN-ACK cuando el usuario termina.
 *
 * COMPILACION (enlazar junto con connections.c, broker y publisher):
 *   gcc -std=c11 -Wall -o sim \
 *       connections.c broker_tcp.c publisher_tcp.c subscriber_tcp.c -lpthread
 *
 * EJECUCION:
 *   ./subscriber <id_conexion>
 *   Ejemplo: ./subscriber 2
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <threads.h>
#include "tcp_sim.h"
/* connections[], conn_count y conn_table_mutex se obtienen
 * via extern desde tcp_sim.h (definidos en connections.c). */
 
 
/* ============================================================
 * HANDSHAKE TCP DE 3 VIAS — LADO CLIENTE (SUBSCRIBER)
 * Identico al del publisher: el suscriptor tambien actua como
 * cliente TCP que se conecta al broker.
 * ============================================================ */
static bool subscriber_connect(tcp_conn_t *conn) {
    tcp_segment_t seg;
 
    /* Esperar que el broker habilite la ranura */
    tcp_wait_state(conn, TCP_LISTEN);
 
    /* --- Paso 1: enviar SYN --- */
    tcp_make_segment(&seg,
                     conn->local_seq,
                     0,
                     FLAG_SYN,
                     NULL, 0);
    tcp_set_state(conn, TCP_SYN_SENT);
    queue_push(&conn->send_queue, &seg);
    printf("[SUB][conn=%d] SYN enviado (seq=%u) → SYN_SENT\n",
           conn->conn_id, conn->local_seq);
 
    /* --- Paso 2: esperar SYN-ACK --- */
    queue_pop(&conn->recv_queue, &seg);
    if (!((seg.flags & FLAG_SYN) && (seg.flags & FLAG_ACK))) {
        fprintf(stderr, "[SUB] Handshake fallido: flags=0x%02X\n", seg.flags);
        return false;
    }
    uint32_t broker_isn = seg.seq_num;
    conn->local_ack     = broker_isn + 1;
    conn->local_seq++;
    printf("[SUB][conn=%d] SYN-ACK recibido (broker_seq=%u)\n",
           conn->conn_id, broker_isn);
 
    /* --- Paso 3: enviar ACK final --- */
    tcp_make_segment(&seg,
                     conn->local_seq,
                     conn->local_ack,
                     FLAG_ACK,
                     NULL, 0);
    queue_push(&conn->send_queue, &seg);
    tcp_set_state(conn, TCP_ESTABLISHED);
    printf("[SUB][conn=%d] Conexion ESTABLISHED\n\n", conn->conn_id);
    return true;
}
 
/* ============================================================
 * HILO DE RECEPCION DE MENSAJES
 * Consume la recv_queue en un bucle dedicado.
 * Por cada segmento FLAG_PSH recibido:
 *   1. Extrae y muestra el payload (mensaje deportivo).
 *   2. Envia un ACK acumulativo al broker.
 *   3. Actualiza los numeros de secuencia locales.
 *
 * Esto simula el buffer de recepcion del kernel TCP:
 *   - Los datos llegan en orden (la cola es FIFO).
 *   - Ningun segmento se pierde (mutex garantiza entrega).
 *   - El ACK confirma la recepcion al emisor.
 * ============================================================ */
static int recv_thread(void *arg) {
    tcp_conn_t *conn = (tcp_conn_t *)arg;
    tcp_segment_t seg;
    int messages_received = 0;  /* Local: cada hilo suscriptor lleva su propio contador */
 
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   ACTUALIZACIONES EN VIVO                ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
 
    while (conn->state == TCP_ESTABLISHED) {
        /* Bloquear hasta recibir el proximo segmento del broker */
        queue_pop(&conn->recv_queue, &seg);
 
        /* --- FIN-ACK: el broker inicio el cierre --- */
        if ((seg.flags & FLAG_FIN) || (seg.flags & FLAG_RST)) {
            printf("[SUB][conn=%d] Conexion cerrada por el broker\n",
                   conn->conn_id);
            tcp_set_state(conn, TCP_CLOSE_WAIT);
 
            /* Enviar ACK del FIN */
            tcp_segment_t ack;
            tcp_make_segment(&ack,
                             conn->local_seq,
                             seg.seq_num + 1,
                             FLAG_ACK,
                             NULL, 0);
            queue_push(&conn->send_queue, &ack);
            tcp_set_state(conn, TCP_CLOSED);
            break;
        }
 
        /* --- Segmento de datos (FLAG_PSH) --- */
        if ((seg.flags & FLAG_PSH) && seg.length > 0) {
            seg.payload[seg.length] = '\0';
            messages_received++;
 
            /* Mostrar el mensaje con formato de notificacion deportiva */
            printf("  🔔 [Msg #%d] %s", messages_received, seg.payload);
 
            /* Actualizar el numero de ACK acumulativo
             * ACK = seq_recibido + bytes_recibidos
             * Esto le indica al broker el proximo byte esperado */
            conn->local_ack = seg.seq_num + seg.length;
 
            /* Enviar ACK de confirmacion al broker */
            tcp_segment_t ack;
            tcp_make_segment(&ack,
                             conn->local_seq,
                             conn->local_ack,    /* ACK acumulativo          */
                             FLAG_ACK,
                             NULL, 0);
            queue_push(&conn->send_queue, &ack);
        }
    }
 
    printf("\n[SUB][conn=%d] Total de mensajes recibidos: %d\n",
           conn->conn_id, messages_received);
    return messages_received;  /* thrd_join() captura este valor */
}
 
/* ============================================================
 * CIERRE ORDENADO — LADO SUSCRIPTOR
 * Envia FLAG_FIN al broker para iniciar el cierre de 4 vias.
 * ============================================================ */
static void subscriber_close(tcp_conn_t *conn) {
    if (conn->state != TCP_ESTABLISHED) return;
 
    tcp_segment_t seg;
    tcp_make_segment(&seg,
                     conn->local_seq,
                     conn->local_ack,
                     FLAG_FIN | FLAG_ACK,
                     NULL, 0);
    tcp_set_state(conn, TCP_FIN_WAIT);
    queue_push(&conn->send_queue, &seg);
    printf("[SUB][conn=%d] FIN enviado → FIN_WAIT\n", conn->conn_id);
 
    /* Esperar que el hilo receptor complete el cierre */
    tcp_wait_state(conn, TCP_CLOSED);
    printf("[SUB][conn=%d] Cierre completado\n", conn->conn_id);
}
 
/* ============================================================
 * PROGRAMA PRINCIPAL DEL SUBSCRIBER
 * ============================================================ */
int subscriber_run(void *arg) {
    sub_args_t *a    = (sub_args_t *)arg;
    int         conn_id = a->conn_id;
 
    srand((unsigned)time(NULL));
 
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) {
        fprintf(stderr, "[SUB] ID de conexion invalido: %d\n", conn_id);
        return thrd_error;
    }
 
    tcp_conn_t *conn = &connections[conn_id];
 
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   SUBSCRIBER TCP  —  Conexion %d          ║\n", conn_id);
    printf("╚══════════════════════════════════════════╝\n\n");
 
    /* Conectar al broker con handshake TCP simulado */
    if (!subscriber_connect(conn)) {
        fprintf(stderr, "[SUB] Fallo el handshake TCP\n");
        exit(EXIT_FAILURE);
    }
 
    /* Lanzar hilo de recepcion de mensajes */
    thrd_t rx_thread;
    thrd_create(&rx_thread, recv_thread, conn);
 
    /* Esperar a que los publicadores terminen (timeout configurado en sub_args_t) */
    struct timespec ts = { (time_t)a->duration_sec, 0L };
    thrd_sleep(&ts, NULL);
 
    /* Cerrar la conexion ordenadamente */
    subscriber_close(conn);
    int msg_count = 0;
    thrd_join(rx_thread, &msg_count);
 
    printf("[SUB] Suscriptor finalizado. Mensajes recibidos: %d\n", msg_count);
    return thrd_success;
}