#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#define UDP_PORT 5001
#define BUFFER_SIZE 1024
#define MAX_SUBSCRIBERS 16
#define ACK_TIMEOUT_SEC 1
#define MAX_RETRIES 5

typedef enum {
    MSG_TYPE_SUBSCRIBE = 1,
    MSG_TYPE_PUBLISH = 2,
    MSG_TYPE_DATA = 3,
    MSG_TYPE_ACK = 4
} MessageType;

typedef struct {
    unsigned int type;
    unsigned int seq;
    char payload[BUFFER_SIZE];
} Packet;

#endif
