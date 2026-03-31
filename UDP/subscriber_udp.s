    .section .data
ip_broker:               .string "127.0.0.1"
msg_registrado:          .string "Subscriber UDP registrado.\n"
fmt_recibido:            .string "Mensaje recibido seq=%u: %s"
txt_socket:              .string "socket"
txt_bind:                .string "bind"
txt_recvfrom:            .string "recvfrom"

    .section .bss
sockfd:                  .space 4
direccion_local:         .space 16
direccion_broker:        .space 16
direccion_origen:        .space 16
longitud_origen:         .space 4
suscripcion:             .space 1032
paquete:                 .space 1032

    .section .text
    .globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp

    # socket(AF_INET, SOCK_DGRAM, 0)
    movl    $2, %edi
    movl    $2, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sockfd(%rip)
    testl   %eax, %eax
    js      .error_socket

    # memset(&direccion_local, 0, 16)
    leaq    direccion_local(%rip), %rdi
    xorl    %esi, %esi
    movl    $16, %edx
    call    memset

    # direccion_local = {AF_INET, puerto 0, INADDR_ANY}
    leaq    direccion_local(%rip), %rbx
    movw    $2, (%rbx)
    movw    $0, 2(%rbx)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    # bind(sockfd, &direccion_local, 16)
    movl    sockfd(%rip), %edi
    leaq    direccion_local(%rip), %rsi
    movl    $16, %edx
    call    bind
    testl   %eax, %eax
    js      .error_bind

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

    # memset(&suscripcion, 0, 1032)
    leaq    suscripcion(%rip), %rdi
    xorl    %esi, %esi
    movl    $1032, %edx
    call    memset

    # suscripcion.type = MSG_TYPE_SUBSCRIBE (1)
    # suscripcion.payload[0] = 'S'
    movl    $1, suscripcion(%rip)
    movb    $'S', suscripcion+8(%rip)
    movb    $0, suscripcion+9(%rip)

    # sendto(sockfd, &suscripcion, 1032, 0, &direccion_broker, 16)
    movl    sockfd(%rip), %edi
    leaq    suscripcion(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    leaq    direccion_broker(%rip), %r8
    movl    $16, %r9d
    call    sendto

    leaq    msg_registrado(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

.bucle_recibir:
    movl    $16, longitud_origen(%rip)

    # recvfrom(sockfd, &paquete, 1032, 0, &direccion_origen, &longitud_origen)
    movl    sockfd(%rip), %edi
    leaq    paquete(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    leaq    direccion_origen(%rip), %r8
    leaq    longitud_origen(%rip), %r9
    call    recvfrom
    testq   %rax, %rax
    js      .error_recvfrom

    cmpl    $3, paquete(%rip)
    jne     .bucle_recibir

    leaq    fmt_recibido(%rip), %rdi
    movl    paquete+4(%rip), %esi
    leaq    paquete+8(%rip), %rdx
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .bucle_recibir

.error_socket:
    leaq    txt_socket(%rip), %rdi
    call    perror
    movl    $1, %eax
    popq    %rbp
    ret

.error_bind:
    leaq    txt_bind(%rip), %rdi
    call    perror
    movl    sockfd(%rip), %edi
    call    close
    movl    $1, %eax
    popq    %rbp
    ret

.error_recvfrom:
    leaq    txt_recvfrom(%rip), %rdi
    call    perror
    movl    sockfd(%rip), %edi
    call    close
    movl    $1, %eax
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
