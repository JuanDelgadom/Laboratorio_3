#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 5000

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[1024];

    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    printf("Suscriptor conectado...\n");

    while (1) {
        int valread = read(sock, buffer, 1024);
        buffer[valread] = '\0';
        printf("Recibido: %s", buffer);
    }

    return 0;
}
