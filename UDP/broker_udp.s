    .section .data
msg_escuchando:          .string "Broker UDP escuchando en puerto 5001...\n"
fmt_publicacion:         .string "Publicacion recibida seq=%u mensaje=%s\n"
fmt_no_soportado:        .string "Tipo de mensaje no soportado: %u\n"
fmt_suscriptor:          .string "Suscriptor registrado: %s:%d\n"
msg_sin_espacio:         .string "No hay espacio para mas suscriptores\n"
txt_socket:              .string "socket"
txt_bind:                .string "bind"
txt_recvfrom:            .string "recvfrom"
txt_sendto:              .string "sendto"

    .section .bss
sockfd:                  .space 4
direccion_servidor:      .space 16
direccion_cliente:       .space 16
longitud_cliente:        .space 4
paquete:                 .space 1032
entrega:                 .space 1032
ack_packet:              .space 1032
suscriptores:            .space 320         # 16 * 20 bytes
ip_buffer:               .space 16

    .section .text
    .globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14

    # socket(AF_INET, SOCK_DGRAM, 0)
    movl    $2, %edi
    movl    $2, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sockfd(%rip)
    testl   %eax, %eax
    js      .error_socket

    # memset(&suscriptores, 0, 320)
    leaq    suscriptores(%rip), %rdi
    xorl    %esi, %esi
    movl    $320, %edx
    call    memset

    # memset(&direccion_servidor, 0, 16)
    leaq    direccion_servidor(%rip), %rdi
    xorl    %esi, %esi
    movl    $16, %edx
    call    memset

    # direccion_servidor = {AF_INET, htons(5001), INADDR_ANY}
    leaq    direccion_servidor(%rip), %rbx
    movw    $2, (%rbx)
    movw    $0x8913, 2(%rbx)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    # bind(sockfd, &direccion_servidor, 16)
    movl    sockfd(%rip), %edi
    leaq    direccion_servidor(%rip), %rsi
    movl    $16, %edx
    call    bind
    testl   %eax, %eax
    js      .error_bind

    leaq    msg_escuchando(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

.bucle_principal:
    movl    $16, longitud_cliente(%rip)

    # recvfrom(sockfd, &paquete, 1032, 0, &direccion_cliente, &longitud_cliente)
    movl    sockfd(%rip), %edi
    leaq    paquete(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    leaq    direccion_cliente(%rip), %r8
    leaq    longitud_cliente(%rip), %r9
    call    recvfrom
    testq   %rax, %rax
    js      .error_recvfrom

    movl    paquete(%rip), %eax
    cmpl    $1, %eax
    je      .manejar_suscripcion
    cmpl    $2, %eax
    je      .manejar_publicacion

    leaq    fmt_no_soportado(%rip), %rdi
    movl    paquete(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .bucle_principal

.manejar_suscripcion:
    leaq    direccion_cliente(%rip), %rdi
    call    agregar_suscriptor
    jmp     .bucle_principal

.manejar_publicacion:
    leaq    fmt_publicacion(%rip), %rdi
    movl    paquete+4(%rip), %esi
    leaq    paquete+8(%rip), %rdx
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

    # enviar_ack(sockfd, &direccion_cliente, seq, 'P')
    movl    sockfd(%rip), %edi
    leaq    direccion_cliente(%rip), %rsi
    movl    paquete+4(%rip), %edx
    movb    $'P', %cl
    call    enviar_ack

    xorl    %r12d, %r12d

.bucle_envio_suscriptores:
    cmpl    $16, %r12d
    jge     .bucle_principal

    leaq    suscriptores(%rip), %rax
    imull   $20, %r12d, %r13d
    movslq  %r13d, %r13
    leaq    (%rax,%r13), %r14

    cmpl    $0, 16(%r14)
    je      .siguiente_suscriptor

    # memset(&entrega, 0, 1032)
    leaq    entrega(%rip), %rdi
    xorl    %esi, %esi
    movl    $1032, %edx
    call    memset

    movl    $3, entrega(%rip)
    movl    paquete+4(%rip), %eax
    movl    %eax, entrega+4(%rip)

    leaq    entrega+8(%rip), %rdi
    leaq    paquete+8(%rip), %rsi
    call    copiar_payload

    # sendto(sockfd, &entrega, 1032, 0, &suscriptor.addr, 16)
    movl    sockfd(%rip), %edi
    leaq    entrega(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    movq    %r14, %r8
    movl    $16, %r9d
    call    sendto
    testq   %rax, %rax
    jns     .siguiente_suscriptor

    leaq    txt_sendto(%rip), %rdi
    call    perror

.siguiente_suscriptor:
    incl    %r12d
    jmp     .bucle_envio_suscriptores

.error_socket:
    leaq    txt_socket(%rip), %rdi
    call    perror
    movl    $1, %eax
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

.error_bind:
    leaq    txt_bind(%rip), %rdi
    call    perror
    movl    sockfd(%rip), %edi
    call    close
    movl    $1, %eax
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

.error_recvfrom:
    leaq    txt_recvfrom(%rip), %rdi
    call    perror
    movl    sockfd(%rip), %edi
    call    close
    movl    $1, %eax
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

# void agregar_suscriptor(struct sockaddr_in *addr)
agregar_suscriptor:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    movq    %rdi, %rbx               # addr
    movq    $-1, %r14                # espacio libre = -1
    xorl    %r12d, %r12d

.agregar_bucle:
    cmpl    $16, %r12d
    jge     .agregar_fin_bucle

    leaq    suscriptores(%rip), %rax
    imull   $20, %r12d, %r13d
    movslq  %r13d, %r13
    leaq    (%rax,%r13), %rax

    cmpl    $0, 16(%rax)
    je      .revisar_libre

    # mismo_extremo(actual.addr, addr)
    movzwl  (%rax), %edx
    movzwl  (%rbx), %ecx
    cmpl    %ecx, %edx
    jne     .siguiente_agregar

    movzwl  2(%rax), %edx
    movzwl  2(%rbx), %ecx
    cmpl    %ecx, %edx
    jne     .siguiente_agregar

    movl    4(%rax), %edx
    movl    4(%rbx), %ecx
    cmpl    %ecx, %edx
    je      .agregar_ya_existe
    jmp     .siguiente_agregar

.revisar_libre:
    cmpq    $-1, %r14
    jne     .siguiente_agregar
    movq    %rax, %r14

.siguiente_agregar:
    incl    %r12d
    jmp     .agregar_bucle

.agregar_fin_bucle:
    cmpq    $-1, %r14
    jne     .guardar_suscriptor

    leaq    msg_sin_espacio(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .agregar_salir

.guardar_suscriptor:
    movq    (%rbx), %rax
    movq    %rax, (%r14)
    movq    8(%rbx), %rax
    movq    %rax, 8(%r14)
    movl    $1, 16(%r14)

    # imprimir_extremo(addr, "Suscriptor registrado: %s:%d\n")
    movl    $2, %edi
    leaq    4(%rbx), %rsi
    leaq    ip_buffer(%rip), %rdx
    movl    $16, %ecx
    call    inet_ntop

    movzwl  2(%rbx), %edi
    call    ntohs

    leaq    fmt_suscriptor(%rip), %rdi
    leaq    ip_buffer(%rip), %rsi
    movl    %eax, %edx
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .agregar_salir

.agregar_ya_existe:
.agregar_salir:
    popq    %r14
    popq    %r13
    popq    %r12
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

# void enviar_ack(int sockfd, struct sockaddr_in *addr, unsigned int seq, char codigo)
enviar_ack:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    movl    %edi, %ebx               # sockfd
    movq    %rsi, %r12               # addr
    movl    %edx, %r13d              # seq
    movb    %cl, %r14b               # codigo

    leaq    ack_packet(%rip), %rdi
    xorl    %esi, %esi
    movl    $1032, %edx
    call    memset

    movl    $4, ack_packet(%rip)
    movl    %r13d, ack_packet+4(%rip)
    movb    %r14b, ack_packet+8(%rip)
    movb    $0, ack_packet+9(%rip)

    # sendto(sockfd, &ack_packet, 1032, 0, addr, 16)
    movl    %ebx, %edi
    leaq    ack_packet(%rip), %rsi
    movl    $1032, %edx
    xorl    %ecx, %ecx
    movq    %r12, %r8
    movl    $16, %r9d
    call    sendto

    testq   %rax, %rax
    jns     .ack_fin

    leaq    txt_sendto(%rip), %rdi
    call    perror

.ack_fin:
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
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
