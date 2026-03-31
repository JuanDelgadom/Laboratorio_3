/*
 * publisher_tcp.c
 * Sistema de Noticias Deportivas — Laboratorio #3
 * Infraestructura de Comunicaciones — Universidad de los Andes
 *
 * ROL: Publicador (periodista deportivo que reporta eventos en vivo).
 *      Se conecta al broker y envía 12 mensajes de partido automaticamente.
 *
 * LIBRERIAS NATIVAS DE C USADAS:
 *   <stdio.h>      - printf(), perror(): trazas y errores.
 *   <stdlib.h>     - exit(): terminacion en caso de error.
 *   <string.h>     - strlen(), snprintf(): construccion de mensajes.
 *   <unistd.h>     - close(), usleep(): cierre de socket y pausas entre mensajes.
 *   <sys/socket.h> - socket(), connect(), send():
 *                    socket()  → crea el socket TCP del cliente.
 *                    connect() → inicia el 3-way handshake TCP con el broker.
 *                    send()    → envia datos al broker por la conexion TCP.
 *   <netinet/in.h> - struct sockaddr_in, htons(): estructura de direccion IPv4.
 *   <arpa/inet.h>  - inet_pton(): convierte "127.0.0.1" a formato binario.
 *
 * COMPILACION:
 *   gcc -std=c11 -Wall -o publisher publisher_tcp.c
 *
 * EJECUCION (broker debe estar corriendo primero):
 *   ./publisher "Colombia vs Brasil"
 *   ./publisher "Argentina vs Uruguay"
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

/* 12 mensajes deportivos automaticos (cumple el requisito de >=10) */
static const char *messages[] = {
    "Inicio del partido — equipos en cancha",
    "Gol! Minuto 12 — delantero anota de cabeza",
    "Tarjeta amarilla al numero 8 — falta sobre el mediocampista",
    "Cambio: jugador 11 entra por jugador 7 al minuto 28",
    "Gol en contra! Minuto 35 — defensa desvía al arco propio",
    "Fin del primer tiempo — marcador 2-1",
    "Inicio del segundo tiempo",
    "Gol! Minuto 54 — penal convertido por el capitan",
    "Tarjeta roja al numero 5 — doble amarilla al minuto 67",
    "Cambio: portero suplente entra al minuto 72",
    "Gol! Minuto 85 — contragolpe letal, 4-1",
    "Pitazo final — victoria contundente"
};
#define NUM_MSGS (int)(sizeof(messages) / sizeof(messages[0]))

int main(int argc, char *argv[]) {
    const char *topic = (argc > 1) ? argv[1] : "Colombia vs Brasil";

    /*
     * socket() — crea un socket TCP.
     *   AF_INET    : direccionamiento IPv4.
     *   SOCK_STREAM: socket orientado a conexion (TCP).
     *   0          : selecciona TCP automaticamente.
     */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in broker;
    memset(&broker, 0, sizeof(broker));
    broker.sin_family = AF_INET;
    broker.sin_port   = htons(BROKER_PORT);
    /* inet_pton() — convierte la IP en formato texto a formato binario */
    inet_pton(AF_INET, BROKER_IP, &broker.sin_addr);

    /*
     * connect() — solicita conexion TCP al broker.
     * El kernel realiza el 3-way handshake (SYN → SYN-ACK → ACK).
     * Esta funcion bloquea hasta que la conexion queda establecida.
     */
    if (connect(fd, (struct sockaddr *)&broker, sizeof(broker)) < 0) {
        perror("connect"); exit(1);
    }

    /* Identificarse como publicador enviando la primera linea */
    char header[256];
    snprintf(header, sizeof(header), "PUB %s\n", topic);
    /* send() — envia bytes al broker a traves de la conexion TCP */
    send(fd, header, strlen(header), 0);

    printf("[PUB] Conectado al broker. Publicando en '%s'\n", topic);

    /* Enviar los 12 mensajes con 300 ms de pausa entre cada uno */
    for (int i = 0; i < NUM_MSGS; i++) {
        char line[MAX_MSG];
        snprintf(line, sizeof(line), "%s\n", messages[i]);
        send(fd, line, strlen(line), 0);
        printf("[PUB] Enviado (%d/%d): %s", i + 1, NUM_MSGS, line);
        sleep(1);   /* 1 segundo — simula cadencia de eventos en vivo */
    }

    /*
     * close() — cierra el socket.
     * El kernel envía el segmento FIN al broker, iniciando el
     * cierre ordenado de la conexion TCP (4-way close).
     */
    close(fd);
    printf("[PUB] Transmision finalizada para '%s'\n", topic);
    return 0;
}

