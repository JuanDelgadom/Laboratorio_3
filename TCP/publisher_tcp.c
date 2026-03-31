/*
 * publisher_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Publicador (periodista deportivo que reporta eventos en vivo).
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>    - printf(), fgets(), stdin: entrada de mensajes y trazas.
 *   <stdlib.h>   - exit(), rand(), srand(): control y numeros aleatorios (ISN).
 *   <string.h>   - strlen(), memcpy(): construccion de payloads de segmentos.
 *   <stdint.h>   - uint32_t, uint16_t, uint8_t: campos del header TCP simulado.
 *   <stdbool.h>  - bool, true, false: control de flujo de la aplicacion.
 *   <time.h>     - time(), thrd_sleep(): ISN y retardos de red simulados.
 *   <threads.h>  - thrd_t, thrd_create, thrd_join: hilo receptor de ACKs.
 *   "tcp_sim.h"  - Protocolo TCP simulado (segmentos, estados, colas).
 *
 * SIMULACION TCP:
 *   - Inicia el handshake de 3 vias como cliente (envia SYN primero).
 *   - Mantiene numeros de secuencia incrementales por cada mensaje.
 *   - Espera ACK del broker antes de enviar el siguiente segmento
 *     (simula la ventana deslizante con window=1 para simplicidad).
 *   - Cierre ordenado con FIN al terminar.
 *
 * COMPILACION:
 *   gcc -std=c11 -Wall -o publisher publisher_tcp.c -lpthread
 *
 * EJECUCION:
 *   ./publisher <id_conexion> <partido>
 *   Ejemplo: ./publisher 0 "Colombia vs Brasil"
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <threads.h>
#include "tcp_sim.h"
 
/* Referencia a la tabla global de conexiones del broker.
 * En un sistema real esto seria un socket FD + IP:puerto.
 * Aqui es un puntero compartido en memoria (mismo proceso). */
extern tcp_conn_t connections[TCP_MAX_CONNS];
 
/* ============================================================
 * HANDSHAKE TCP DE 3 VIAS — LADO CLIENTE (PUBLISHER)
 *
 * Paso 1: Publisher envia SYN con su ISN
 *         → FLAG_SYN, seq=ISN_pub, ack=0
 * Paso 2: Publisher espera SYN-ACK del broker
 *         → FLAG_SYN|FLAG_ACK, seq=ISN_broker, ack=ISN_pub+1
 * Paso 3: Publisher envia ACK final
 *         → FLAG_ACK, seq=ISN_pub+1, ack=ISN_broker+1
 * Resultado: estado ESTABLISHED en ambos extremos.
 * ============================================================ */
static bool publisher_connect(tcp_conn_t *conn) {
    tcp_segment_t seg;
 
    /* Esperar a que el broker marque la ranura como LISTEN */
    tcp_wait_state(conn, TCP_LISTEN);
 
    /* --- Paso 1: enviar SYN --- */
    tcp_make_segment(&seg,
                     conn->local_seq,   /* ISN del publisher                */
                     0,                 /* ACK = 0 en SYN inicial            */
                     FLAG_SYN,
                     NULL, 0);
    tcp_set_state(conn, TCP_SYN_SENT);
    queue_push(&conn->send_queue, &seg);
    printf("[PUB][conn=%d] SYN enviado (seq=%u) → SYN_SENT\n",
           conn->conn_id, conn->local_seq);
 
    /* --- Paso 2: esperar SYN-ACK del broker --- */
    queue_pop(&conn->recv_queue, &seg);
    if (!((seg.flags & FLAG_SYN) && (seg.flags & FLAG_ACK))) {
        fprintf(stderr, "[PUB] Error en handshake: flags inesperados 0x%02X\n",
                seg.flags);
        return false;
    }
    uint32_t broker_isn = seg.seq_num;
    conn->local_ack     = broker_isn + 1;   /* Proximo ACK esperado del broker */
    conn->local_seq++;                       /* Avanzar seq post-SYN            */
    printf("[PUB][conn=%d] SYN-ACK recibido (broker_seq=%u) → enviando ACK\n",
           conn->conn_id, broker_isn);
 
    /* --- Paso 3: enviar ACK final --- */
    tcp_make_segment(&seg,
                     conn->local_seq,
                     conn->local_ack,
                     FLAG_ACK,
                     NULL, 0);
    queue_push(&conn->send_queue, &seg);
    tcp_set_state(conn, TCP_ESTABLISHED);
    printf("[PUB][conn=%d] Conexion ESTABLISHED\n", conn->conn_id);
    return true;
}
 
/* ============================================================
 * HILO RECEPTOR DE ACKs
 * En TCP real el kernel gestiona los ACKs de forma transparente.
 * Aqui un hilo dedicado los consume para no bloquear el emisor.
 * Tambien detecta FIN-ACK del broker durante el cierre.
 * ============================================================ */
static int ack_receiver_thread(void *arg) {
    tcp_conn_t *conn = (tcp_conn_t *)arg;
    tcp_segment_t seg;
 
    while (conn->state == TCP_ESTABLISHED || conn->state == TCP_FIN_WAIT) {
        queue_pop(&conn->recv_queue, &seg);
 
        if (seg.flags & FLAG_ACK && !(seg.flags & FLAG_FIN)) {
            /* ACK de datos: confirma que el broker recibio el segmento */
            printf("[PUB][conn=%d] ACK recibido (ack_num=%u)\n",
                   conn->conn_id, seg.ack_num);
        }
 
        if ((seg.flags & FLAG_FIN) && (seg.flags & FLAG_ACK)) {
            /* FIN-ACK: broker confirma cierre */
            printf("[PUB][conn=%d] FIN-ACK recibido → conexion cerrada\n",
                   conn->conn_id);
            tcp_set_state(conn, TCP_CLOSED);
            break;
        }
    }
    return 0;
}
 
/* ============================================================
 * ENVIO DE SEGMENTO DE DATOS
 * Empaqueta el mensaje en un segmento TCP con FLAG_PSH|FLAG_ACK
 * y lo coloca en la cola de envio hacia el broker.
 * Actualiza el numero de secuencia local (avance del stream).
 * ============================================================ */
static void publisher_send(tcp_conn_t *conn, const char *msg) {
    if (conn->state != TCP_ESTABLISHED) {
        fprintf(stderr, "[PUB] No se puede enviar: estado=%s\n",
                tcp_state_name(conn->state));
        return;
    }
 
    uint16_t len = (uint16_t)strlen(msg);
    if (len >= TCP_MAX_PAYLOAD) len = TCP_MAX_PAYLOAD - 1;
 
    tcp_segment_t seg;
    tcp_make_segment(&seg,
                     conn->local_seq,   /* Numero de secuencia actual        */
                     conn->local_ack,   /* ACK acumulativo al broker         */
                     FLAG_PSH | FLAG_ACK,
                     msg, len);
 
    conn->local_seq += len;             /* Avanzar seq: bytes enviados       */
    queue_push(&conn->send_queue, &seg);
    printf("[PUB][conn=%d] Segmento enviado (seq=%u, len=%u): %s",
           conn->conn_id, seg.seq_num, len, msg);
}
 
/* ============================================================
 * CIERRE ORDENADO TCP (equivale al FIN de 4 vias)
 * El publisher envia FLAG_FIN y espera FIN-ACK del broker.
 * ============================================================ */
static void publisher_close(tcp_conn_t *conn) {
    if (conn->state != TCP_ESTABLISHED) return;
 
    tcp_segment_t seg;
    tcp_make_segment(&seg,
                     conn->local_seq,
                     conn->local_ack,
                     FLAG_FIN | FLAG_ACK,
                     NULL, 0);
    tcp_set_state(conn, TCP_FIN_WAIT);
    queue_push(&conn->send_queue, &seg);
    printf("[PUB][conn=%d] FIN enviado → FIN_WAIT\n", conn->conn_id);
 
    /* Esperar confirmacion del broker */
    tcp_wait_state(conn, TCP_CLOSED);
    printf("[PUB][conn=%d] Cierre completado\n", conn->conn_id);
}
 
/* ============================================================
 * PROGRAMA PRINCIPAL DEL PUBLISHER
 * ============================================================ */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: ./publisher <id_conexion 0-%d> <\"partido\">\n",
                TCP_MAX_CONNS - 1);
        fprintf(stderr, "Ejemplo: ./publisher 0 \"Colombia vs Brasil\"\n");
        exit(EXIT_FAILURE);
    }
 
    srand((unsigned)time(NULL));
 
    int conn_id = atoi(argv[1]);
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) {
        fprintf(stderr, "[PUB] ID de conexion invalido: %d\n", conn_id);
        exit(EXIT_FAILURE);
    }
 
    const char *partido = argv[2];
    tcp_conn_t *conn    = &connections[conn_id];
 
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   PUBLISHER TCP  —  Conexion %d           ║\n", conn_id);
    printf("║   Partido: %-30s ║\n", partido);
    printf("╚══════════════════════════════════════════╝\n\n");
 
    /* Conectar al broker mediante handshake TCP simulado */
    if (!publisher_connect(conn)) {
        fprintf(stderr, "[PUB] Fallo el handshake TCP\n");
        exit(EXIT_FAILURE);
    }
 
    /* Lanzar hilo receptor de ACKs */
    thrd_t ack_thread;
    thrd_create(&ack_thread, ack_receiver_thread, conn);
 
    /* Bucle de publicacion: leer mensajes de stdin y enviarlos */
    char input[TCP_MAX_PAYLOAD];
    char message[TCP_MAX_PAYLOAD + 64];
 
    printf("\nEscribe eventos del partido (ENTER para enviar, 'q' para salir):\n");
    printf("Ejemplos: 'Gol de Colombia al minuto 32'\n\n");
 
    while (1) {
        printf("[%s] > ", partido);
        if (!fgets(input, sizeof(input), stdin)) break;
 
        /* Quitar el newline al final */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';
 
        if (input[0] == 'q' && input[1] == '\0') break;
        if (input[0] == '\0') continue;
 
        /* Formatear el mensaje con el nombre del partido */
        snprintf(message, sizeof(message), "[%s] %s\n", partido, input);
        publisher_send(conn, message);
 
        /* Pausa minima para simular latencia de red */
        struct timespec ts = {0, 5000000L}; /* 5 ms */
        thrd_sleep(&ts, NULL);
    }
 
    /* Cerrar la conexion ordenadamente */
    publisher_close(conn);
    thrd_join(ack_thread, NULL);
 
    printf("[PUB] Publisher finalizado.\n");
    return 0;
}