#ifndef TCP_SIM_H
#define TCP_SIM_H
 
/*
 * tcp_sim.h
 * Simulacion del protocolo TCP usando unicamente librerias nativas de C11.
 *
 * Librerias usadas:
 *   <stdint.h>    - uint32_t, uint16_t: tipos de ancho fijo para campos del header TCP
 *   <stdbool.h>   - bool, true, false: flags del segmento TCP
 *   <string.h>    - memset(), memcpy(), strlen(): manejo de buffers
 *   <stdlib.h>    - rand(), srand(): generacion de numero de secuencia inicial (ISN)
 *   <time.h>      - time(): semilla para rand() al generar ISN
 *   <threads.h>   - mtx_t, cnd_t, thrd_t: mutex, variables de condicion e hilos C11
 *                   Reemplazan el canal de red: la sincronizacion entre hilos
 *                   equivale a la entrega garantizada y ordenada de TCP.
 *   <stdio.h>     - printf(): trazas de depuracion del protocolo
 */
 
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <threads.h>
#include <stdio.h>
 
/* ============================================================
 * CONSTANTES DEL PROTOCOLO
 * ============================================================ */
#define TCP_MAX_PAYLOAD   512   /* Bytes maximos de datos por segmento (MSS) */
#define TCP_WINDOW_SIZE   4     /* Ventana de control de flujo (segmentos)   */
#define TCP_MAX_CONNS     10    /* Conexiones simultaneas maximas en el broker*/
#define TCP_QUEUE_SIZE    16    /* Tamano de la cola circular de segmentos    */
#define TCP_MAX_RETRIES   5     /* Reintentos antes de declarar error         */
 
/* ============================================================
 * FLAGS TCP (campo de control del header)
 * Equivalen a los bits SYN, ACK, FIN, RST del header real TCP
 * ============================================================ */
#define FLAG_SYN  0x01   /* Synchronize: inicia conexion (handshake paso 1 y 2) */
#define FLAG_ACK  0x02   /* Acknowledge: confirma recepcion de datos             */
#define FLAG_FIN  0x04   /* Finish: cierre ordenado de la conexion               */
#define FLAG_RST  0x08   /* Reset: cierre abrupto / error                        */
#define FLAG_PSH  0x10   /* Push: entregar datos inmediatamente a la aplicacion  */
 
/* ============================================================
 * ESTADOS DE LA CONEXION TCP (maquina de estados)
 * Refleja la FSM (Finite State Machine) del RFC 793
 * ============================================================ */
typedef enum {
    TCP_CLOSED,       /* Sin conexion                                     */
    TCP_LISTEN,       /* Servidor esperando SYN (post-listen)             */
    TCP_SYN_SENT,     /* Cliente envio SYN, espera SYN-ACK                */
    TCP_SYN_RECEIVED, /* Servidor recibio SYN, envio SYN-ACK, espera ACK  */
    TCP_ESTABLISHED,  /* Conexion establecida, flujo de datos activo       */
    TCP_FIN_WAIT,     /* Inicio de cierre: se envio FIN, espera ACK        */
    TCP_CLOSE_WAIT,   /* Se recibio FIN del par, pendiente de cerrar local */
    TCP_TIME_WAIT,    /* Espera final antes de CLOSED (2*MSL simulado)     */
} tcp_state_t;
 
/* ============================================================
 * SEGMENTO TCP
 * Equivale al header + payload del segmento TCP real (RFC 793)
 * ============================================================ */
typedef struct {
    uint32_t seq_num;              /* Numero de secuencia del primer byte de datos  */
    uint32_t ack_num;              /* Numero de ACK: siguiente byte esperado         */
    uint16_t window;               /* Ventana de recepcion: control de flujo         */
    uint8_t  flags;                /* Combinacion de FLAG_SYN, FLAG_ACK, etc.        */
    uint16_t length;               /* Longitud del payload en bytes                  */
    char     payload[TCP_MAX_PAYLOAD]; /* Datos de la aplicacion                    */
} tcp_segment_t;
 
/* ============================================================
 * COLA CIRCULAR DE SEGMENTOS
 * Simula el buffer de recepcion/envio del kernel TCP.
 * El mutex y la variable de condicion reemplazan la red:
 * garantizan entrega ordenada (equivalente al reordenamiento
 * de segmentos de TCP) y ausencia de perdida (equivalente a
 * la retransmision de TCP).
 * ============================================================ */
typedef struct {
    tcp_segment_t segments[TCP_QUEUE_SIZE]; /* Buffer circular          */
    int           head;                     /* Indice de lectura        */
    int           tail;                     /* Indice de escritura      */
    int           count;                    /* Segmentos disponibles    */
    mtx_t         mutex;                    /* Exclusion mutua          */
    cnd_t         not_empty;                /* Senala: hay datos        */
    cnd_t         not_full;                 /* Senala: hay espacio      */
} tcp_queue_t;
 
/* ============================================================
 * CONEXION TCP
 * Representa un canal full-duplex entre dos extremos.
 * Cada conexion tiene dos colas: una para cada direccion.
 * ============================================================ */
typedef struct {
    int          conn_id;        /* Identificador de la conexion              */
    tcp_state_t  state;          /* Estado actual en la FSM de TCP            */
    uint32_t     local_seq;      /* Numero de secuencia local (proximo envio) */
    uint32_t     remote_seq;     /* Ultimo numero de secuencia recibido       */
    uint32_t     local_ack;      /* Proximo ACK a enviar                      */
    uint16_t     send_window;    /* Ventana de envio (control de flujo)       */
    uint16_t     recv_window;    /* Ventana de recepcion anunciada al par     */
    tcp_queue_t  send_queue;     /* Cola de segmentos a enviar                */
    tcp_queue_t  recv_queue;     /* Cola de segmentos recibidos               */
    mtx_t        state_mutex;    /* Protege el estado de la FSM               */
    cnd_t        state_cond;     /* Notifica cambios de estado                */
} tcp_conn_t;
 
/* ============================================================
 * FUNCIONES DE LA COLA CIRCULAR
 * ============================================================ */
 
/* Inicializa la cola y sus primitivas de sincronizacion */
static inline void queue_init(tcp_queue_t *q) {
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    mtx_init(&q->mutex,     mtx_plain);
    cnd_init(&q->not_empty);
    cnd_init(&q->not_full);
}
 
/* Encola un segmento (bloqueante si la cola esta llena — control de flujo) */
static inline void queue_push(tcp_queue_t *q, const tcp_segment_t *seg) {
    mtx_lock(&q->mutex);
    while (q->count == TCP_QUEUE_SIZE)          /* Esperar espacio libre    */
        cnd_wait(&q->not_full, &q->mutex);
    memcpy(&q->segments[q->tail], seg, sizeof(tcp_segment_t));
    q->tail  = (q->tail + 1) % TCP_QUEUE_SIZE;
    q->count++;
    cnd_signal(&q->not_empty);                  /* Notificar al lector      */
    mtx_unlock(&q->mutex);
}
 
/* Desencola un segmento (bloqueante si la cola esta vacia) */
static inline void queue_pop(tcp_queue_t *q, tcp_segment_t *seg) {
    mtx_lock(&q->mutex);
    while (q->count == 0)                       /* Esperar datos            */
        cnd_wait(&q->not_empty, &q->mutex);
    memcpy(seg, &q->segments[q->head], sizeof(tcp_segment_t));
    q->head  = (q->head + 1) % TCP_QUEUE_SIZE;
    q->count--;
    cnd_signal(&q->not_full);                   /* Notificar al escritor    */
    mtx_unlock(&q->mutex);
}
 
/* Destruye la cola y libera sus primitivas */
static inline void queue_destroy(tcp_queue_t *q) {
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->not_empty);
    cnd_destroy(&q->not_full);
}
 
/* ============================================================
 * FUNCIONES DE LA CONEXION TCP
 * ============================================================ */
 
/* Inicializa una conexion en estado CLOSED */
static inline void tcp_conn_init(tcp_conn_t *conn, int id) {
    conn->conn_id     = id;
    conn->state       = TCP_CLOSED;
    conn->local_seq   = (uint32_t)(rand() % 100000); /* ISN aleatorio       */
    conn->remote_seq  = 0;
    conn->local_ack   = 0;
    conn->send_window = TCP_WINDOW_SIZE;
    conn->recv_window = TCP_WINDOW_SIZE;
    queue_init(&conn->send_queue);
    queue_init(&conn->recv_queue);
    mtx_init(&conn->state_mutex, mtx_plain);
    cnd_init(&conn->state_cond);
}
 
/* Construye un segmento TCP con los campos dados */
static inline void tcp_make_segment(tcp_segment_t *seg,
                                    uint32_t seq, uint32_t ack,
                                    uint8_t flags,
                                    const char *data, uint16_t data_len) {
    seg->seq_num = seq;
    seg->ack_num = ack;
    seg->window  = TCP_WINDOW_SIZE;
    seg->flags   = flags;
    seg->length  = data_len;
    if (data && data_len > 0)
        memcpy(seg->payload, data, data_len);
}
 
/* Cambia el estado de la FSM de forma segura */
static inline void tcp_set_state(tcp_conn_t *conn, tcp_state_t new_state) {
    mtx_lock(&conn->state_mutex);
    conn->state = new_state;
    cnd_broadcast(&conn->state_cond);
    mtx_unlock(&conn->state_mutex);
}
 
/* Espera hasta que la conexion alcance un estado especifico */
static inline void tcp_wait_state(tcp_conn_t *conn, tcp_state_t target) {
    mtx_lock(&conn->state_mutex);
    while (conn->state != target)
        cnd_wait(&conn->state_cond, &conn->state_mutex);
    mtx_unlock(&conn->state_mutex);
}
 
/* Nombre textual del estado (para trazas de depuracion) */
static inline const char *tcp_state_name(tcp_state_t s) {
    switch (s) {
        case TCP_CLOSED:       return "CLOSED";
        case TCP_LISTEN:       return "LISTEN";
        case TCP_SYN_SENT:     return "SYN_SENT";
        case TCP_SYN_RECEIVED: return "SYN_RECEIVED";
        case TCP_ESTABLISHED:  return "ESTABLISHED";
        case TCP_FIN_WAIT:     return "FIN_WAIT";
        case TCP_CLOSE_WAIT:   return "CLOSE_WAIT";
        case TCP_TIME_WAIT:    return "TIME_WAIT";
        default:               return "UNKNOWN";
    }
}
 
#endif /* TCP_SIM_H */