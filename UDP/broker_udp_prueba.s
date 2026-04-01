    .section .data
msg_escuchando:          .string "Broker UDP de prueba escuchando en puerto 5001...\n"
msg_prueba:              .string "Este broker pierde el primer ACK al publisher y el primer envio al subscriber.\n"
fmt_publicacion:         .string "Publicacion recibida seq=%u mensaje=%s\n"
fmt_no_soportado:        .string "Tipo de mensaje no soportado: %u\n"
fmt_suscriptor:          .string "Suscriptor registrado: %s:%d\n"
msg_sin_espacio:         .string "No hay espacio para mas suscriptores\n"
fmt_perdida_ack:         .string "Prueba: se pierde a proposito el ACK del publisher para seq=%u\n"
fmt_perdida_envio:       .string "Prueba: se pierde a proposito el envio al subscriber para seq=%u\n"
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
suscriptores:            .space 320
ip_buffer:               .space 16
perder_primer_ack:       .space 4
perder_primer_envio:     .space 4

    .section .text
    .globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14

    movl    $1, perder_primer_ack(%rip)
    movl    $1, perder_primer_envio(%rip)

    movl    $2, %edi
    movl    $2, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sockfd(%rip)
    testl   %eax, %eax
    js      .error_socket

    leaq    suscriptores(%rip), %rdi
    xorl    %esi, %esi
    movl    $320, %edx
    call    memset

    leaq    direccion_servidor(%rip), %rdi
    xorl    %esi, %esi
    movl    $16, %edx
    call    memset

    leaq    direccion_servidor(%rip), %rbx
    movw    $2, (%rbx)
    movw    $0x8913, 2(%rbx)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    movl    sockfd(%rip), %edi
    leaq    direccion_servidor(%rip), %rsi
    movl    $16, %edx
    call    bind
    testl   %eax, %eax
    js      .error_bind

    leaq    msg_escuchando(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    leaq    msg_prueba(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout

.bucle_principal:
    movl    $16, longitud_cliente(%rip)

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

    cmpl    $0, perder_primer_ack(%rip)
    je      .ack_normal
    movl    $0, perder_primer_ack(%rip)
    leaq    fmt_perdida_ack(%rip), %rdi
    movl    paquete+4(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .enviar_a_suscriptores

.ack_normal:
    movl    sockfd(%rip), %edi
    leaq    direccion_cliente(%rip), %rsi
    movl    paquete+4(%rip), %edx
    movb    $'P', %cl
    call    enviar_ack

.enviar_a_suscriptores:
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

    cmpl    $0, perder_primer_envio(%rip)
    je      .envio_normal
    movl    $0, perder_primer_envio(%rip)
    leaq    fmt_perdida_envio(%rip), %rdi
    movl    entrega+4(%rip), %esi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
    jmp     .siguiente_suscriptor

.envio_normal:
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

agregar_suscriptor:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    movq    %rdi, %rbx
    movq    $-1, %r14
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

enviar_ack:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    movl    %edi, %ebx
    movq    %rsi, %r12
    movl    %edx, %r13d
    movb    %cl, %r14b

    leaq    ack_packet(%rip), %rdi
    xorl    %esi, %esi
    movl    $1032, %edx
    call    memset

    movl    $4, ack_packet(%rip)
    movl    %r13d, ack_packet+4(%rip)
    movb    %r14b, ack_packet+8(%rip)
    movb    $0, ack_packet+9(%rip)

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
