/*
 * subscriber_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Suscriptor (hincha que sigue el partido en tiempo real).
 *      Se conecta al broker, se suscribe a un tema y recibe
 *      actualizaciones en vivo hasta que el broker cierre la sesion.
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>      - printf(), perror(): mostrar mensajes recibidos y errores.
 *   <stdlib.h>     - exit(): terminacion en caso de error.
 *   <string.h>     - strlen(), snprintf(), memset(): buffers y mensajes.
 *   <unistd.h>     - close(): cierre del socket al terminar.
 *   <sys/socket.h> - socket(), connect(), send(), recv():
 *                    socket()  crea el socket TCP del cliente.
 *                    connect() realiza el 3-way handshake con el broker.
 *                    send()    envia la linea de suscripcion al broker.
 *                    recv()    recibe actualizaciones del broker en tiempo real.
 *   <netinet/in.h> - struct sockaddr_in, htons(): estructura de direccion IPv4.
 *   <arpa/inet.h>  - inet_pton(): convierte IP de texto a binario.
 *
 * COMPILACION:
 *   gcc -std=c11 -Wall -o subscriber subscriber_tcp.c
 *
 * EJECUCION (broker debe estar corriendo primero):
 *   ./subscriber "Colombia vs Brasil"
 *   ./subscriber "Argentina vs Uruguay"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BROKER_IP   "127.0.0.1"
#define BROKER_PORT 8080
#define MAX_MSG     512

int main(int argc, char *argv[]) {
    const char *topic = (argc > 1) ? argv[1] : "Colombia vs Brasil";

    /*
     * socket() - crea un socket TCP.
     *   AF_INET    : dominio IPv4.
     *   SOCK_STREAM: tipo orientado a conexion (TCP).
     *   0          : protocolo seleccionado por el sistema (TCP).
     */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in broker;
    memset(&broker, 0, sizeof(broker));
    broker.sin_family = AF_INET;
    broker.sin_port   = htons(BROKER_PORT);
    /* inet_pton() - convierte la cadena "127.0.0.1" a formato binario de red */
    inet_pton(AF_INET, BROKER_IP, &broker.sin_addr);

    /*
     * connect() - inicia la conexion TCP con el broker.
     * El kernel realiza el 3-way handshake (SYN -> SYN-ACK -> ACK).
     * Bloquea hasta que la conexion queda completamente establecida.
     */
    if (connect(fd, (struct sockaddr *)&broker, sizeof(broker)) < 0) {
        perror("connect"); exit(1);
    }

    /* Enviar linea de suscripcion al broker */
    char header[256];
    snprintf(header, sizeof(header), "SUB %s\n", topic);
    /* send() - transmite la solicitud de suscripcion al broker */
    send(fd, header, strlen(header), 0);

    printf("[SUB] Suscrito a '%s'. Esperando actualizaciones...\n\n", topic);

    /*
     * Bucle de recepcion: lee byte a byte hasta '\n' y muestra el mensaje.
     * recv() retorna 0 cuando el broker cierra la conexion (FIN TCP),
     * lo que termina el bucle.
     */
    char buf[MAX_MSG];
    int  n = 0;
    char c;
    while (1) {
        /* recv() - recibe datos del broker via socket TCP */
        int r = recv(fd, &c, 1, 0);
        if (r <= 0) break;   /* 0 = conexion cerrada, <0 = error */

        if (c == '\n') {
            buf[n] = '\0';
            printf("[SUB] %s\n", buf);
            n = 0;
        } else if (n < MAX_MSG - 1) {
            buf[n++] = c;
        }
    }

    /*
     * close() - cierra el socket del suscriptor.
     * El kernel inicia el cierre TCP enviando un segmento FIN.
     */
    close(fd);
    printf("[SUB] Conexion cerrada. Fin de la transmision.\n");
    return 0;
}
