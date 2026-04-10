#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "quic_common.h"

typedef struct {
    struct sockaddr_in addr;
    int activo;
    unsigned int ultimo_seq_entregado;
} Suscriptor;

static int mismo_extremo(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static void imprimir_extremo(const struct sockaddr_in *addr, const char *etiqueta) {
    char ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    printf("%s %s:%d\n", etiqueta, ip, ntohs(addr->sin_port));
}

static void copiar_payload(char *destino, const char *origen) {
    int i = 0;

    while (i < BUFFER_SIZE - 1 && origen[i] != '\0') {
        destino[i] = origen[i];
        i++;
    }

    destino[i] = '\0';
}

static void agregar_suscriptor(Suscriptor suscriptores[], const struct sockaddr_in *addr) {
    int espacio_libre = -1;

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (suscriptores[i].activo && mismo_extremo(&suscriptores[i].addr, addr)) {
            return;
        }

        if (!suscriptores[i].activo && espacio_libre == -1) {
            espacio_libre = i;
        }
    }

    if (espacio_libre == -1) {
        printf("No hay espacio para mas suscriptores\n");
        return;
    }

    suscriptores[espacio_libre].activo = 1;
    suscriptores[espacio_libre].addr = *addr;
    suscriptores[espacio_libre].ultimo_seq_entregado = 0;
    imprimir_extremo(addr, "Suscriptor QUIC registrado:");
}

static void enviar_ack(int sockfd, const struct sockaddr_in *addr, unsigned int seq, char codigo_ack) {
    Packet ack = {0};

    ack.type = MSG_TYPE_ACK;
    ack.seq = seq;
    ack.payload[0] = codigo_ack;
    ack.payload[1] = '\0';

    sendto(sockfd, &ack, sizeof(ack), 0, (const struct sockaddr *)addr, sizeof(*addr));
}

static int esperar_ack_suscriptor(int sockfd,
                                  Suscriptor suscriptores[],
                                  const struct sockaddr_in *suscriptor,
                                  unsigned int seq) {
    Packet paquete;
    struct sockaddr_in direccion_origen;
    socklen_t longitud_origen = sizeof(direccion_origen);
    fd_set descriptores_lectura;
    struct timeval tiempo_espera;

    while (1) {
        FD_ZERO(&descriptores_lectura);
        FD_SET(sockfd, &descriptores_lectura);
        tiempo_espera.tv_sec = ACK_TIMEOUT_SEC;
        tiempo_espera.tv_usec = 0;

        if (select(sockfd + 1, &descriptores_lectura, NULL, NULL, &tiempo_espera) <= 0) {
            return 0;
        }

        longitud_origen = sizeof(direccion_origen);
        if (recvfrom(sockfd,
                     &paquete,
                     sizeof(paquete),
                     0,
                     (struct sockaddr *)&direccion_origen,
                     &longitud_origen) < 0) {
            return 0;
        }

        if (paquete.type == MSG_TYPE_SUBSCRIBE) {
            agregar_suscriptor(suscriptores, &direccion_origen);
            continue;
        }

        if (paquete.type == MSG_TYPE_ACK &&
            paquete.seq == seq &&
            paquete.payload[0] == 'S' &&
            mismo_extremo(&direccion_origen, suscriptor)) {
            return 1;
        }
    }
}

static void entregar_a_suscriptor(int sockfd,
                                  Suscriptor suscriptores[],
                                  Suscriptor *suscriptor,
                                  unsigned int seq,
                                  const char *payload) {
    Packet entrega = {0};
    int ack_recibido = 0;

    entrega.type = MSG_TYPE_DATA;
    entrega.seq = seq;
    copiar_payload(entrega.payload, payload);

    for (int intento = 0; intento <= MAX_RETRIES; intento++) {
        if (sendto(sockfd,
                   &entrega,
                   sizeof(entrega),
                   0,
                   (struct sockaddr *)&suscriptor->addr,
                   sizeof(suscriptor->addr)) < 0) {
            perror("sendto");
            return;
        }

        if (esperar_ack_suscriptor(sockfd, suscriptores, &suscriptor->addr, seq)) {
            ack_recibido = 1;
            suscriptor->ultimo_seq_entregado = seq;
            break;
        }

        if (intento < MAX_RETRIES) {
            printf("Reenviando seq=%u al suscriptor %d\n", seq, ntohs(suscriptor->addr.sin_port));
        }
    }

    if (!ack_recibido) {
        printf("No se recibio ACK del suscriptor para seq=%u\n", seq);
    }
}

int main(void) {
    int sockfd;
    struct sockaddr_in direccion_servidor, direccion_cliente;
    socklen_t longitud_cliente = sizeof(direccion_cliente);
    Suscriptor suscriptores[MAX_SUBSCRIBERS] = {0};
    Packet paquete;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;
    direccion_servidor.sin_port = htons(QUIC_PORT);

    if (bind(sockfd, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Broker QUIC escuchando en puerto %d...\n", QUIC_PORT);

    while (1) {
        longitud_cliente = sizeof(direccion_cliente);
        ssize_t recibido = recvfrom(sockfd,
                                    &paquete,
                                    sizeof(paquete),
                                    0,
                                    (struct sockaddr *)&direccion_cliente,
                                    &longitud_cliente);

        if (recibido < 0) {
            perror("recvfrom");
            break;
        }

        switch (paquete.type) {
            case MSG_TYPE_SUBSCRIBE:
                agregar_suscriptor(suscriptores, &direccion_cliente);
                break;

            case MSG_TYPE_PUBLISH:
                printf("Publicacion QUIC recibida seq=%u mensaje=%s\n", paquete.seq, paquete.payload);
                enviar_ack(sockfd, &direccion_cliente, paquete.seq, 'P');

                for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                    if (!suscriptores[i].activo) {
                        continue;
                    }

                    entregar_a_suscriptor(sockfd,
                                          suscriptores,
                                          &suscriptores[i],
                                          paquete.seq,
                                          paquete.payload);
                }
                break;

            case MSG_TYPE_ACK:
                break;

            default:
                printf("Tipo de mensaje no soportado: %u\n", paquete.type);
                break;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
