#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp_common.h"

static void enviar_suscripcion(int sockfd, const struct sockaddr_in *direccion_broker) {
    Packet suscripcion = {0};

    suscripcion.type = MSG_TYPE_SUBSCRIBE;
    suscripcion.payload[0] = 'S';
    suscripcion.payload[1] = '\0';

    sendto(sockfd,
           &suscripcion,
           sizeof(suscripcion),
           0,
           (const struct sockaddr *)direccion_broker,
           sizeof(*direccion_broker));
}

int main(void) {
    int sockfd;
    struct sockaddr_in direccion_local, direccion_broker, direccion_origen;
    socklen_t longitud_origen = sizeof(direccion_origen);
    Packet paquete;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&direccion_local, 0, sizeof(direccion_local));
    direccion_local.sin_family = AF_INET;
    direccion_local.sin_addr.s_addr = INADDR_ANY;
    direccion_local.sin_port = htons(0);

    if (bind(sockfd, (struct sockaddr *)&direccion_local, sizeof(direccion_local)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    memset(&direccion_broker, 0, sizeof(direccion_broker));
    direccion_broker.sin_family = AF_INET;
    direccion_broker.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &direccion_broker.sin_addr);

    enviar_suscripcion(sockfd, &direccion_broker);
    printf("Subscriber UDP registrado.\n");

    while (1) {
        longitud_origen = sizeof(direccion_origen);
        ssize_t recibido = recvfrom(sockfd,
                                    &paquete,
                                    sizeof(paquete),
                                    0,
                                    (struct sockaddr *)&direccion_origen,
                                    &longitud_origen);

        if (recibido < 0) {
            perror("recvfrom");
            break;
        }

        if (paquete.type != MSG_TYPE_DATA) {
            continue;
        }

        printf("Mensaje recibido seq=%u: %s", paquete.seq, paquete.payload);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
