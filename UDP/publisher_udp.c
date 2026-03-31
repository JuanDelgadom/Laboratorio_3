#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp_common.h"

static void copiar_payload(char *destino, const char *origen) {
    int i = 0;

    while (i < BUFFER_SIZE - 1 && origen[i] != '\0') {
        destino[i] = origen[i];
        i++;
    }

    destino[i] = '\0';
}

static const char *mensajes[] = {
    "Inicio del partido\n",
    "Gol al minuto 12\n",
    "Tarjeta amarilla al numero 8\n",
    "Cambio al minuto 28\n",
    "Gol en contra al minuto 35\n",
    "Fin del primer tiempo\n",
    "Inicia el segundo tiempo\n",
    "Gol de penal al minuto 54\n",
    "Tarjeta roja al minuto 67\n",
    "Cambio de portero al minuto 72\n",
    "Gol al minuto 85\n",
    "Final del partido\n"
};

#define NUM_MENSAJES ((int)(sizeof(mensajes) / sizeof(mensajes[0])))

static int esperar_ack(int sockfd, unsigned int seq) {
    Packet paquete;
    struct sockaddr_in direccion_origen;
    socklen_t longitud_origen = sizeof(direccion_origen);
    fd_set descriptores_lectura;
    struct timeval tiempo_espera;

    FD_ZERO(&descriptores_lectura);
    FD_SET(sockfd, &descriptores_lectura);
    tiempo_espera.tv_sec = ACK_TIMEOUT_SEC;
    tiempo_espera.tv_usec = 0;

    if (select(sockfd + 1, &descriptores_lectura, NULL, NULL, &tiempo_espera) <= 0) {
        return 0;
    }

    if (recvfrom(sockfd,
                 &paquete,
                 sizeof(paquete),
                 0,
                 (struct sockaddr *)&direccion_origen,
                 &longitud_origen) < 0) {
        return 0;
    }

    return paquete.type == MSG_TYPE_ACK &&
           paquete.seq == seq &&
           paquete.payload[0] == 'P';
}

int main(void) {
    int sockfd;
    struct sockaddr_in direccion_broker;
    Packet paquete;
    unsigned int seq = 1;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&direccion_broker, 0, sizeof(direccion_broker));
    direccion_broker.sin_family = AF_INET;
    direccion_broker.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &direccion_broker.sin_addr);

    printf("Publisher UDP listo. Enviando mensajes automaticamente.\n");

    for (int indice = 0; indice < NUM_MENSAJES; indice++) {
        int ack_recibido = 0;

        memset(&paquete, 0, sizeof(paquete));
        paquete.type = MSG_TYPE_PUBLISH;
        paquete.seq = seq;
        copiar_payload(paquete.payload, mensajes[indice]);

        for (int intento = 0; intento <= MAX_RETRIES; intento++) {
            if (sendto(sockfd,
                       &paquete,
                       sizeof(paquete),
                       0,
                       (struct sockaddr *)&direccion_broker,
                       sizeof(direccion_broker)) < 0) {
                perror("sendto");
                break;
            }

            if (esperar_ack(sockfd, seq)) {
                printf("Mensaje publicado con ACK seq=%u: %s", seq, mensajes[indice]);
                ack_recibido = 1;
                break;
            }

            if (intento < MAX_RETRIES) {
                printf("No llego ACK del broker, reenviando seq=%u\n", seq);
            }
        }

        if (!ack_recibido) {
            printf("No se recibio ACK final para seq=%u\n", seq);
        }

        sleep(1);
        seq++;
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
