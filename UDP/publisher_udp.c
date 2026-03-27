#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp_common.h"

static void copy_payload(char *dest, const char *src) {
    int i = 0;

    while (i < BUFFER_SIZE - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }

    dest[i] = '\0';
}

int main(void) {
    int sockfd;
    struct sockaddr_in broker_addr;
    Packet packet;
    char input[BUFFER_SIZE];
    unsigned int seq = 1;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &broker_addr.sin_addr);

    printf("Publisher UDP listo. Escribe mensajes para publicar.\n");

    while (fgets(input, sizeof(input), stdin) != NULL) {
        memset(&packet, 0, sizeof(packet));
        packet.type = MSG_TYPE_PUBLISH;
        packet.seq = seq;
        copy_payload(packet.payload, input);

        if (sendto(sockfd,
                   &packet,
                   sizeof(packet),
                   0,
                   (struct sockaddr *)&broker_addr,
                   sizeof(broker_addr)) < 0) {
            perror("sendto");
            continue;
        }

        printf("Mensaje publicado seq=%u\n", seq);

        seq++;
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
