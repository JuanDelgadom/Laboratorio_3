    .section .data
ip_broker:               .string "127.0.0.1"
msg_inicio:              .string "Publisher UDP listo. Escribe mensajes para publicar.\n"
fmt_ack_ok:              .string "Mensaje publicado con ACK seq=%u\n"
fmt_reenvio:             .string "No llego ACK del broker, reenviando seq=%u\n"
fmt_ack_fallo:           .string "No se recibio ACK final para seq=%u\n"
txt_socket:              .string "socket"
txt_sendto:              .string "sendto"

    .section .bss
sockfd:                  .space 4
direccion_broker:        .space 16
paquete:                 .space 1032
paquete_ack:             .space 1032
direccion_origen:        .space 16
longitud_origen:         .space 4
entrada:                 .space 1024
readfds:                 .space 128
tiempo_espera:           .space 16
secuencia:               .space 4

    .section .text
    .globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp

    movl    $1, secuencia(%rip)

    # socket(AF_INET, SOCK_DGRAM, 0)
    movl    $2, %edi
    movl    $2, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sockfd(%rip)
    testl   %eax, %eax
    js      .error_socket

    # memset(&direccion_broker, 0, 16)
    leaq    direccion_broker(%rip), %rdi
    xorl    %esi, %esi
    movl    $16, %edx
    call    memset

    # direccion_broker.sin_family = AF_INET
    # direccion_broker.sin_port = htons(5001) -> 0x8913
    leaq    direccion_broker(%rip), %rbx
    movw    $2, (%rbx)
    movw    $0x8913, 2(%rbx)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    # inet_pton(AF_INET, "127.0.0.1", &direccion_broker.sin_addr)
    movl    $2, %edi
    leaq    ip_broker(%rip), %rsi
    leaq    direccion_broker+4(%rip), %rdx
    call    inet_pton

    leaq    msg_inicio(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

.bucle_lectura:
    # fgets(entrada, 1024, stdin)
    leaq    entrada(%rip), %rdi
    movl    $1024, %esi
    movq    stdin(%rip), %rdx
    call    fgets
    testq   %rax, %rax
    jz      .fin

    # memset(&paquete, 0, 1032)
    leaq    paquete(%rip), %rdi
    xorl    %esi, %esi
    movl    $1032, %edx
    call    memset

    # paquete.type = MSG_TYPE_PUBLISH (2)
    # paquete.seq = secuencia
    movl    $2, paquete(%rip)
    movl    secuencia(%rip), %eax
    movl    %eax, paquete+4(%rip)

    # copiar payload a paquete.payload
    leaq    paquete+8(%rip), %rdi
    leaq    entrada(%rip), %rsi
    call    copiar_payload

    xorl    %r12d, %r12d        # intento = 0

.bucle_envio:
    cmpl    $5, %r12d
    jg      .sin_ack

    # sendto(sockfd, &paquete, 1032, 0, &direccion_broker, 16)
    movl    sockfd(%rip), %edi
    leaq    paquete(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    leaq    direccion_broker(%rip), %r8
    movl    $16, %r9d
    call    sendto
    testq   %rax, %rax
    js      .error_sendto

    movl    secuencia(%rip), %edi
    call    esperar_ack
    testl   %eax, %eax
    jnz     .ack_recibido

    cmpl    $5, %r12d
    jge     .sin_ack

    leaq    fmt_reenvio(%rip), %rdi
    movl    secuencia(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

    incl    %r12d
    jmp     .bucle_envio

.ack_recibido:
    leaq    fmt_ack_ok(%rip), %rdi
    movl    secuencia(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .siguiente_mensaje

.sin_ack:
    leaq    fmt_ack_fallo(%rip), %rdi
    movl    secuencia(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .siguiente_mensaje

.siguiente_mensaje:
    incl    secuencia(%rip)
    jmp     .bucle_lectura

.error_socket:
    leaq    txt_socket(%rip), %rdi
    call    perror
    movl    $1, %eax
    popq    %rbp
    ret

.error_sendto:
    leaq    txt_sendto(%rip), %rdi
    call    perror
    jmp     .siguiente_mensaje

.fin:
    movl    sockfd(%rip), %edi
    call    close
    xorl    %eax, %eax
    popq    %rbp
    ret

# int esperar_ack(unsigned int seq)
esperar_ack:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    movl    %edi, %ebx

    # FD_ZERO(&readfds)
    leaq    readfds(%rip), %rdi
    xorl    %esi, %esi
    movl    $128, %edx
    call    memset

    # FD_SET(sockfd, &readfds)
    movl    sockfd(%rip), %edi
    leaq    readfds(%rip), %rsi
    call    __fd_set_manual

    # timeout = {1, 0}
    movq    $1, tiempo_espera(%rip)
    movq    $0, tiempo_espera+8(%rip)

    # select(sockfd+1, &readfds, NULL, NULL, &tiempo_espera)
    movl    sockfd(%rip), %edi
    incl    %edi
    leaq    readfds(%rip), %rsi
    xorl    %edx, %edx
    xorl    %ecx, %ecx
    leaq    tiempo_espera(%rip), %r8
    call    select
    cmpl    $0, %eax
    jle     .ack_no

    movl    $16, longitud_origen(%rip)

    # recvfrom(sockfd, &paquete_ack, 1032, 0, &direccion_origen, &longitud_origen)
    movl    sockfd(%rip), %edi
    leaq    paquete_ack(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    leaq    direccion_origen(%rip), %r8
    leaq    longitud_origen(%rip), %r9
    call    recvfrom
    testq   %rax, %rax
    js      .ack_no

    cmpl    $4, paquete_ack(%rip)
    jne     .ack_no
    movl    paquete_ack+4(%rip), %eax
    cmpl    %ebx, %eax
    jne     .ack_no
    movb    paquete_ack+8(%rip), %al
    cmpb    $'P', %al
    jne     .ack_no

    movl    $1, %eax
    jmp     .ack_fin

.ack_no:
    xorl    %eax, %eax

.ack_fin:
    popq    %rbx
    popq    %rbp
    ret

# void copiar_payload(char *destino, const char *origen)
copiar_payload:
    pushq   %rbp
    movq    %rsp, %rbp
    xorl    %ecx, %ecx

.copiar_bucle:
    cmpl    $1023, %ecx
    jge     .copiar_fin
    movb    (%rsi,%rcx,1), %al
    movb    %al, (%rdi,%rcx,1)
    cmpb    $0, %al
    je      .copiar_fin
    incl    %ecx
    jmp     .copiar_bucle

.copiar_fin:
    movb    $0, (%rdi,%rcx,1)
    popq    %rbp
    ret

# void __fd_set_manual(int fd, fd_set *set)
__fd_set_manual:
    pushq   %rbp
    movq    %rsp, %rbp
    movl    %edi, %eax
    movl    %eax, %r8d
    shrl    $6, %r8d
    andl    $63, %eax
    movl    %eax, %ecx
    movq    $1, %rdx
    shlq    %cl, %rdx
    movslq  %r8d, %r8
    orq     %rdx, (%rsi,%r8,8)
    popq    %rbp
    ret

fflush_stdout:
    pushq   %rbp
    movq    %rsp, %rbp
    movq    stdout(%rip), %rdi
    call    fflush
    popq    %rbp
    ret

    .section .note.GNU-stack,"",@progbits
