#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp_common.h"

static void send_subscribe(int sockfd, const struct sockaddr_in *broker_addr) {
    Packet subscribe = {0};

    subscribe.type = MSG_TYPE_SUBSCRIBE;
    subscribe.payload[0] = 'S';
    subscribe.payload[1] = '\0';

    sendto(sockfd,
           &subscribe,
           sizeof(subscribe),
           0,
           (const struct sockaddr *)broker_addr,
           sizeof(*broker_addr));
}

int main(void) {
    int sockfd;
    struct sockaddr_in local_addr, broker_addr, from_addr;
    socklen_t from_len = sizeof(from_addr);
    Packet packet;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(0);

    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &broker_addr.sin_addr);

    send_subscribe(sockfd, &broker_addr);
    printf("Subscriber UDP registrado.\n");

    while (1) {
        from_len = sizeof(from_addr);
        ssize_t received = recvfrom(sockfd,
                                    &packet,
                                    sizeof(packet),
                                    0,
                                    (struct sockaddr *)&from_addr,
                                    &from_len);

        if (received < 0) {
            perror("recvfrom");
            break;
        }

        if (packet.type != MSG_TYPE_DATA) {
            continue;
        }

        printf("Mensaje recibido seq=%u: %s", packet.seq, packet.payload);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
