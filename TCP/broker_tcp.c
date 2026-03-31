/*
 * broker_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Broker central del modelo publicacion-suscripcion.
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>    - printf(), fprintf(): salida de trazas del protocolo.
 *   <stdlib.h>   - exit(), rand(), srand(): control de flujo y numeros aleatorios.
 *   <string.h>   - memset(), memcpy(), strlen(): manejo de buffers de segmentos.
 *   <stdbool.h>  - bool, true, false: flags del header TCP simulado.
 *   <stdint.h>   - uint32_t, uint16_t, uint8_t: tipos del header TCP.
 *   <time.h>     - time(), struct timespec, thrd_sleep(): ISN y retardos simulados.
 *   <threads.h>  - thrd_t, mtx_t, cnd_t: hilos, mutex y variables de condicion.
 *                  Los hilos reemplazan los sockets: cada conexion es un par de
 *                  colas sincronizadas que simulan el canal TCP full-duplex.
 *   "tcp_sim.h"  - Estructuras y funciones del protocolo TCP simulado.
 *
 * SIMULACION TCP:
 *   - Handshake de 3 vias (SYN / SYN-ACK / ACK) por cada cliente que se conecta.
 *   - Numeros de secuencia y ACK incrementales por cada mensaje enviado.
 *   - Control de flujo: ventana deslizante (TCP_WINDOW_SIZE segmentos).
 *   - Cierre ordenado con FIN / ACK (equivalente al cierre de 4 vias de TCP).
 *   - Cada cliente tiene su propio hilo receptor en el broker (como un FD TCP).
 *
 * COMPILACION:
 *   gcc -std=c11 -Wall -o broker broker_tcp.c -lpthread
 *
 * EJECUCION:
 *   ./broker
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <threads.h>
#include "tcp_sim.h"
 
/* ============================================================
 * TABLA GLOBAL DE CONEXIONES
 * Equivale a la tabla de sockets del kernel para el servidor.
 * Cada entrada es un canal TCP full-duplex con un cliente.
 * ============================================================ */
static tcp_conn_t connections[TCP_MAX_CONNS];
static int        conn_count = 0;
static mtx_t      conn_table_mutex;   /* Protege conn_count y connections[]  */
 
/* ============================================================
 * HANDSHAKE TCP DE 3 VIAS — LADO SERVIDOR (BROKER)
 *
 * Paso 1: Broker recibe SYN del cliente
 *         → cliente envia FLAG_SYN con su ISN (seq=ISN_cliente)
 * Paso 2: Broker responde SYN-ACK
 *         → FLAG_SYN|FLAG_ACK, seq=ISN_broker, ack=ISN_cliente+1
 * Paso 3: Broker recibe ACK del cliente
 *         → FLAG_ACK, ack=ISN_broker+1
 * Resultado: estado ESTABLISHED en ambos extremos.
 * ============================================================ */
static void broker_handshake(tcp_conn_t *conn) {
    tcp_segment_t seg;
 
    /* --- Paso 1: esperar SYN del cliente --- */
    queue_pop(&conn->send_queue, &seg);   /* send_queue del cliente llega aqui */
    if (!(seg.flags & FLAG_SYN)) {
        fprintf(stderr, "[BROKER] Error: esperaba SYN, flags=0x%02X\n", seg.flags);
        tcp_set_state(conn, TCP_CLOSED);
        return;
    }
    conn->remote_seq = seg.seq_num;
    conn->local_ack  = seg.seq_num + 1;  /* ACK = ISN_cliente + 1 */
    printf("[BROKER][conn=%d] SYN recibido (seq=%u) → estado SYN_RECEIVED\n",
           conn->conn_id, seg.seq_num);
    tcp_set_state(conn, TCP_SYN_RECEIVED);
 
    /* --- Paso 2: enviar SYN-ACK al cliente --- */
    tcp_make_segment(&seg,
                     conn->local_seq,    /* seq  = ISN_broker                */
                     conn->local_ack,    /* ack  = ISN_cliente + 1           */
                     FLAG_SYN | FLAG_ACK,
                     NULL, 0);
    queue_push(&conn->recv_queue, &seg); /* recv_queue del cliente            */
    printf("[BROKER][conn=%d] SYN-ACK enviado (seq=%u, ack=%u)\n",
           conn->conn_id, conn->local_seq, conn->local_ack);
 
    /* --- Paso 3: esperar ACK final del cliente --- */
    queue_pop(&conn->send_queue, &seg);
    if (!(seg.flags & FLAG_ACK)) {
        fprintf(stderr, "[BROKER] Error: esperaba ACK final, flags=0x%02X\n", seg.flags);
        tcp_set_state(conn, TCP_CLOSED);
        return;
    }
    conn->local_seq++;                   /* Avanzar seq propio post-SYN      */
    printf("[BROKER][conn=%d] ACK recibido → estado ESTABLISHED\n", conn->conn_id);
    tcp_set_state(conn, TCP_ESTABLISHED);
}
 
/* ============================================================
 * BROADCAST: reenviar segmento a todos los clientes conectados
 * excepto al emisor original.
 * Simula la funcion del broker en pub-sub: distribuir mensajes.
 * ============================================================ */
static void broker_broadcast(int sender_id, const tcp_segment_t *seg) {
    mtx_lock(&conn_table_mutex);
    for (int i = 0; i < conn_count; i++) {
        if (connections[i].conn_id == sender_id) continue;
        if (connections[i].state  != TCP_ESTABLISHED) continue;
 
        /* Construir segmento de datos con FLAG_PSH (entrega inmediata) */
        tcp_segment_t out;
        tcp_make_segment(&out,
                         connections[i].local_seq,
                         connections[i].local_ack,
                         FLAG_PSH | FLAG_ACK,
                         seg->payload,
                         seg->length);
        connections[i].local_seq += seg->length;  /* Avanzar numero de seq  */
        queue_push(&connections[i].recv_queue, &out);
        printf("[BROKER] Mensaje reenviado a conn=%d (%u bytes)\n",
               connections[i].conn_id, seg->length);
    }
    mtx_unlock(&conn_table_mutex);
}
 
/* ============================================================
 * HILO RECEPTOR POR CONEXION
 * Cada cliente tiene un hilo dedicado en el broker que:
 *   1. Ejecuta el handshake TCP.
 *   2. Lee segmentos de datos en bucle.
 *   3. Hace broadcast de cada mensaje.
 *   4. Cierra la conexion al recibir FIN.
 *
 * Esto es equivalente al modelo de un hilo por socket aceptado
 * en un servidor TCP real.
 * ============================================================ */
static int broker_client_thread(void *arg) {
    tcp_conn_t *conn = (tcp_conn_t *)arg;
 
    /* Ejecutar handshake antes de aceptar datos */
    broker_handshake(conn);
    if (conn->state != TCP_ESTABLISHED) return 1;
 
    tcp_segment_t seg;
    while (1) {
        /* Esperar el proximo segmento del cliente */
        queue_pop(&conn->send_queue, &seg);
 
        /* --- FIN: cierre ordenado (equivale al FIN de TCP 4-way close) --- */
        if (seg.flags & FLAG_FIN) {
            printf("[BROKER][conn=%d] FIN recibido → enviando ACK de cierre\n",
                   conn->conn_id);
 
            /* Enviar ACK del FIN */
            tcp_segment_t ack_seg;
            tcp_make_segment(&ack_seg,
                             conn->local_seq,
                             seg.seq_num + 1,
                             FLAG_ACK | FLAG_FIN,
                             NULL, 0);
            queue_push(&conn->recv_queue, &ack_seg);
            tcp_set_state(conn, TCP_CLOSE_WAIT);
 
            /* Breve pausa simulando TIME_WAIT */
            struct timespec ts = {0, 50000000L}; /* 50 ms */
            thrd_sleep(&ts, NULL);
            tcp_set_state(conn, TCP_CLOSED);
            printf("[BROKER][conn=%d] Conexion cerrada\n", conn->conn_id);
            break;
        }
 
        /* --- PSH|ACK: segmento de datos --- */
        if ((seg.flags & FLAG_PSH) && seg.length > 0) {
            seg.payload[seg.length] = '\0';
            printf("[BROKER][conn=%d] Datos recibidos (%u bytes): %s",
                   conn->conn_id, seg.length, seg.payload);
 
            /* Enviar ACK de confirmacion al emisor */
            tcp_segment_t ack_seg;
            tcp_make_segment(&ack_seg,
                             conn->local_seq,
                             seg.seq_num + seg.length,  /* ACK acumulativo   */
                             FLAG_ACK,
                             NULL, 0);
            queue_push(&conn->recv_queue, &ack_seg);
            conn->local_ack = seg.seq_num + seg.length;
 
            /* Distribuir a todos los suscriptores */
            broker_broadcast(conn->conn_id, &seg);
        }
    }
    return 0;
}
 
/* ============================================================
 * HILO PRINCIPAL DEL BROKER
 * Simula accept() en un bucle: espera que los clientes
 * inicialicen una conexion y lanza un hilo receptor por cada una.
 * ============================================================ */
int main(void) {
    srand((unsigned)time(NULL));
    mtx_init(&conn_table_mutex, mtx_plain);
 
    /* Pre-inicializar todas las conexiones */
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        tcp_conn_init(&connections[i], i);
 
    printf("╔══════════════════════════════════════╗\n");
    printf("║   BROKER TCP  —  Puerto virtual 5000 ║\n");
    printf("║   Simulacion con hilos C11 (threads) ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("[BROKER] Listo para aceptar conexiones (max=%d)\n\n", TCP_MAX_CONNS);
 
    /*
     * El broker queda en estado LISTEN (equivalente a listen() de sockets).
     * En este modelo de simulacion, publishers y subscribers obtienen un
     * puntero a una conexion pre-inicializada en lugar de llamar a connect().
     * El bucle principal monitorea nuevas conexiones que pasen a SYN_SENT.
     */
    thrd_t threads[TCP_MAX_CONNS];
    bool   launched[TCP_MAX_CONNS];
    for (int i = 0; i < TCP_MAX_CONNS; i++) launched[i] = false;
 
    /* Marcar todas como LISTEN para que los clientes puedan conectarse */
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        tcp_set_state(&connections[i], TCP_LISTEN);
 
    /* Bucle de aceptacion: detectar nuevas conexiones en SYN_SENT */
    while (1) {
        struct timespec ts = {0, 10000000L}; /* Polling cada 10 ms */
        thrd_sleep(&ts, NULL);
 
        mtx_lock(&conn_table_mutex);
        for (int i = 0; i < TCP_MAX_CONNS; i++) {
            if (!launched[i] && connections[i].state == TCP_SYN_SENT) {
                printf("[BROKER] Nueva conexion detectada en ranura %d → lanzando hilo\n", i);
                thrd_create(&threads[i], broker_client_thread, &connections[i]);
                launched[i] = true;
                if (i >= conn_count) conn_count = i + 1;
            }
        }
        mtx_unlock(&conn_table_mutex);
    }
 
    mtx_destroy(&conn_table_mutex);
    return 0;
}