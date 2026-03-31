#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#define UDP_PORT 5001
#define BUFFER_SIZE 1024
#define MAX_SUBSCRIBERS 16

typedef enum {
    MSG_TYPE_SUBSCRIBE = 1,
    MSG_TYPE_PUBLISH = 2,
    MSG_TYPE_DATA = 3
} MessageType;

typedef struct {
    unsigned int type;
    unsigned int seq;
    char payload[BUFFER_SIZE];
} Packet;

#endif
