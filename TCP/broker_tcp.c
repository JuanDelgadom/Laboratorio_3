/*
 * broker_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Broker central del modelo publicacion-suscripcion.
 *      Recibe mensajes de publicadores y los reenvía a los
 *      suscriptores registrados en el mismo tema.
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>        - printf(), fprintf(), perror(): trazas y errores.
 *   <stdlib.h>       - exit(), malloc(), free(): gestion de memoria.
 *   <string.h>       - memset(), strncpy(), strcmp(), strlen(): buffers y temas.
 *   <unistd.h>       - close(), read(), write(): cierre de descriptores.
 *   <pthread.h>      - pthread_t, pthread_create(), pthread_mutex_*: hilos y mutex.
 *   <sys/socket.h>   - socket(), bind(), listen(), accept(), send(), recv():
 *                      API principal de sockets POSIX.
 *                      socket()  → crea el socket TCP.
 *                      bind()    → asocia el socket al puerto 8080.
 *                      listen()  → habilita la cola de conexiones entrantes.
 *                      accept()  → acepta cada nueva conexion TCP (3-way handshake
 *                                  completado por el kernel).
 *                      recv()    → lee datos del cliente sobre la conexion TCP.
 *                      send()    → envia datos al cliente sobre la conexion TCP.
 *   <netinet/in.h>   - struct sockaddr_in, htons(), INADDR_ANY: direcciones IPv4.
 *   <arpa/inet.h>    - inet_ntoa(): imprime la IP del cliente conectado.
 *
 * PROTOCOLO DE MENSAJES (linea de texto terminada en '\n'):
 *   - Primera linea del cliente: "PUB <tema>" o "SUB <tema>"
 *   - Publisher envia: "<texto del mensaje>\n"
 *   - Broker reenvía a subscribers: "[<tema>] <texto>\n"
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
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BROKER_PORT  8080
#define BACKLOG      16
#define MAX_CLIENTS  64
#define MAX_TOPIC    128
#define MAX_MSG      512

/* ============================================================
 * TABLA DE SUSCRIPTORES ACTIVOS
 * El broker mantiene una lista de sockets de suscriptores
 * junto con el tema al que cada uno esta suscrito.
 * El mutex protege acceso concurrente desde multiples hilos.
 * ============================================================ */
typedef struct {
    int  fd;
    char topic[MAX_TOPIC];
    int  active;
} sub_entry_t;

static sub_entry_t     subs[MAX_CLIENTS];
static int             sub_count  = 0;
static pthread_mutex_t sub_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================
 * BROADCAST: enviar el mensaje a todos los suscriptores del tema.
 * Usa send() para escribir sobre cada socket TCP abierto.
 * Si send() falla, el suscriptor se marca como inactivo y se
 * cierra su socket con close().
 * ============================================================ */
static void broadcast(const char *topic, const char *msg) {
    char line[MAX_TOPIC + MAX_MSG + 8];
    snprintf(line, sizeof(line), "[%s] %s\n", topic, msg);

    pthread_mutex_lock(&sub_mutex);
    for (int i = 0; i < sub_count; i++) {
        if (!subs[i].active) continue;
        if (strcmp(subs[i].topic, topic) != 0) continue;

        /* send() — envia los datos al suscriptor via socket TCP */
        if (send(subs[i].fd, line, strlen(line), 0) < 0) {
            subs[i].active = 0;
            close(subs[i].fd);
        }
    }
    pthread_mutex_unlock(&sub_mutex);
}

/* ============================================================
 * HILO POR CONEXION ENTRANTE
 * Cada cliente (publisher o subscriber) recibe su propio hilo.
 * 1. Lee la primera linea para determinar el tipo (PUB/SUB).
 * 2. Si es SUB: registra el socket en la tabla y espera cierre.
 * 3. Si es PUB: lee mensajes en bucle y los reenvía via broadcast.
 * ============================================================ */
static void *handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    /* Leer primera linea: "PUB <tema>" o "SUB <tema>" */
    char buf[MAX_TOPIC + 8];
    int  n = 0;
    char c;
    while (n < (int)sizeof(buf) - 1) {
        /* recv() — recibe un byte del cliente */
        if (recv(fd, &c, 1, 0) <= 0) { close(fd); return NULL; }
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';

    char type[4], topic[MAX_TOPIC];
    if (sscanf(buf, "%3s %127[^\n]", type, topic) != 2) {
        close(fd); return NULL;
    }

    if (strcmp(type, "SUB") == 0) {
        /* Registrar suscriptor en la tabla */
        pthread_mutex_lock(&sub_mutex);
        int idx = sub_count++;
        subs[idx].fd     = fd;
        subs[idx].active = 1;
        strncpy(subs[idx].topic, topic, MAX_TOPIC - 1);
        pthread_mutex_unlock(&sub_mutex);

        printf("[BROKER] Suscriptor registrado en '%s'\n", topic);

        /* Mantener el socket abierto: esperar hasta que el cliente cierre */
        char dummy;
        while (recv(fd, &dummy, 1, 0) > 0);

        pthread_mutex_lock(&sub_mutex);
        subs[idx].active = 0;
        pthread_mutex_unlock(&sub_mutex);
        close(fd);
        printf("[BROKER] Suscriptor de '%s' se desconecto\n", topic);

    } else if (strcmp(type, "PUB") == 0) {
        printf("[BROKER] Publicador conectado para '%s'\n", topic);

        /* Leer mensajes del publisher y reenviarlos a suscriptores */
        char msg[MAX_MSG];
        int  m = 0;
        while (1) {
            /* recv() — lee byte a byte hasta '\n' */
            int r = recv(fd, &c, 1, 0);
            if (r <= 0) break;
            if (c == '\n') {
                msg[m] = '\0';
                if (m > 0) {
                    printf("[BROKER] '%s' → '%s'\n", topic, msg);
                    broadcast(topic, msg);
                }
                m = 0;
            } else if (m < MAX_MSG - 1) {
                msg[m++] = c;
            }
        }
        close(fd);
        printf("[BROKER] Publicador de '%s' se desconecto\n", topic);

    } else {
        close(fd);
    }

    return NULL;
}

/* ============================================================
 * MAIN — BUCLE PRINCIPAL DEL BROKER
 * ============================================================ */
int main(void) {
    /*
     * socket() — crea el socket TCP.
     *   AF_INET    : dominio IPv4.
     *   SOCK_STREAM: tipo de socket orientado a conexion (TCP).
     *   0          : protocolo seleccionado automaticamente (TCP).
     * Retorna un descriptor de archivo (file descriptor).
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    /*
     * setsockopt() — configura opciones del socket.
     *   SO_REUSEADDR: permite reusar el puerto inmediatamente despues
     *   de cerrar el broker, evitando el error "Address already in use".
     */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   /* escuchar en todas las interfaces */
    addr.sin_port        = htons(BROKER_PORT);

    /*
     * bind() — asocia el socket a la direccion y puerto especificados.
     * Despues de bind(), el kernel sabe que las conexiones al puerto
     * BROKER_PORT deben entregarse a este proceso.
     */
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    /*
     * listen() — pone el socket en modo pasivo (escucha).
     * BACKLOG define el maximo de conexiones pendientes en la cola
     * antes de que el kernel empiece a rechazarlas.
     * El 3-way handshake TCP lo completa el kernel automaticamente.
     */
    if (listen(server_fd, BACKLOG) < 0) { perror("listen"); exit(1); }

    printf("[BROKER] Escuchando en 0.0.0.0:%d (TCP)...\n", BROKER_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /*
         * accept() — extrae la primera conexion completada de la cola.
         * Bloquea hasta que llega una nueva conexion TCP.
         * Retorna un NUEVO socket descriptor para comunicarse con ese
         * cliente especifico; el socket original sigue escuchando.
         */
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) { perror("accept"); free(client_fd); continue; }

        printf("[BROKER] Nueva conexion desde %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Lanzar hilo para manejar este cliente */
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}