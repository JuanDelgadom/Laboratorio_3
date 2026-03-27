#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "udp_common.h"

typedef struct {
    struct sockaddr_in addr;
    int active;
} Subscriber;

static int same_endpoint(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static void print_endpoint(const struct sockaddr_in *addr, const char *label) {
    char ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    printf("%s %s:%d\n", label, ip, ntohs(addr->sin_port));
}

static void add_subscriber(Subscriber subscribers[], const struct sockaddr_in *addr) {
    int free_slot = -1;

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].active && same_endpoint(&subscribers[i].addr, addr)) {
            return;
        }

        if (!subscribers[i].active && free_slot == -1) {
            free_slot = i;
        }
    }

    if (free_slot == -1) {
        printf("No hay espacio para mas suscriptores\n");
        return;
    }

    subscribers[free_slot].active = 1;
    subscribers[free_slot].addr = *addr;
    print_endpoint(addr, "Suscriptor registrado:");
}

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
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    Subscriber subscribers[MAX_SUBSCRIBERS] = {0};
    Packet packet;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(UDP_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Broker UDP escuchando en puerto %d...\n", UDP_PORT);

    while (1) {
        client_len = sizeof(client_addr);
        ssize_t received = recvfrom(sockfd,
                                    &packet,
                                    sizeof(Packet),
                                    0,
                                    (struct sockaddr *)&client_addr,
                                    &client_len);

        if (received < 0) {
            perror("recvfrom");
            break;
        }

        switch (packet.type) {
            case MSG_TYPE_SUBSCRIBE:
                add_subscriber(subscribers, &client_addr);
                break;

            case MSG_TYPE_PUBLISH:
                printf("Publicacion recibida seq=%u mensaje=%s\n",
                       packet.seq,
                       packet.payload);

                for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                    Packet delivery = {0};

                    if (!subscribers[i].active) {
                        continue;
                    }

                    delivery.type = MSG_TYPE_DATA;
                    delivery.seq = packet.seq;
                    copy_payload(delivery.payload, packet.payload);

                    if (sendto(sockfd,
                               &delivery,
                               sizeof(delivery),
                               0,
                               (struct sockaddr *)&subscribers[i].addr,
                               sizeof(subscribers[i].addr)) < 0) {
                        perror("sendto");
                    }
                }
                break;

            default:
                printf("Tipo de mensaje no soportado: %u\n", packet.type);
                break;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
