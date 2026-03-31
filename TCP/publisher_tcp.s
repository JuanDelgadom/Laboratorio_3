.section .data
ip_str:         .string "127.0.0.1"
port:           .word   0x1F90              # 8080 en network byte order

msg_connected:  .string "[PUB] Conectado al broker\n"

header:         .string "PUB Colombia vs Brasil\n"

msg1: .string "Inicio del partido\n"
msg2: .string "Gol minuto 12\n"
msg3: .string "Tarjeta amarilla\n"
msg4: .string "Fin del primer tiempo\n"
msg5: .string "Inicio segundo tiempo\n"
msg6: .string "Gol minuto 54\n"
msg7: .string "Tarjeta roja\n"
msg8: .string "Cambio de jugador\n"
msg9: .string "Gol minuto 85\n"
msg10:.string "Final del partido\n"

msgs:
    .quad msg1, msg2, msg3, msg4, msg5
    .quad msg6, msg7, msg8, msg9, msg10

num_msgs: .long 10


.section .bss
sock:       .space 4
serv_addr:  .space 16


.section .text
.globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp

    # ── socket(AF_INET=2, SOCK_STREAM=1, 0) ──
    movl    $2, %edi
    movl    $1, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sock(%rip)

    # ── configurar sockaddr_in ──
    leaq    serv_addr(%rip), %rbx
    movw    $2, (%rbx)            # AF_INET
    movw    port(%rip), %ax
    movw    %ax, 2(%rbx)          # puerto
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    # inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr)
    movl    $2, %edi
    leaq    ip_str(%rip), %rsi
    leaq    serv_addr+4(%rip), %rdx
    call    inet_pton

    # ── connect ──
    movl    sock(%rip), %edi
    leaq    serv_addr(%rip), %rsi
    movl    $16, %edx
    call    connect

    # ── printf conectado ──
    leaq    msg_connected(%rip), %rdi
    xorl    %eax, %eax
    call    printf

    # ── enviar header ──
    movl    sock(%rip), %edi
    leaq    header(%rip), %rsi
    call    strlen
    movq    %rax, %rdx
    leaq    header(%rip), %rsi
    xorl    %ecx, %ecx
    call    send

    # ── loop de mensajes ──
    movl    $0, %r13d

loop_msgs:
    cmpl    num_msgs(%rip), %r13d
    jge     end_publisher

    # msg = msgs[i]
    movslq  %r13d, %rax
    leaq    msgs(%rip), %rbx
    movq    (%rbx,%rax,8), %r14

    # len = strlen(msg)
    movq    %r14, %rdi
    call    strlen
    movq    %rax, %rdx

    # send(sock, msg, len, 0)
    movl    sock(%rip), %edi
    movq    %r14, %rsi
    xorl    %ecx, %ecx
    call    send

    # sleep(1)
    movl    $1, %edi
    call    sleep

    incl    %r13d
    jmp     loop_msgs


end_publisher:
    movl    sock(%rip), %edi
    call    close

    xorl    %eax, %eax
    popq    %rbp
    ret

.section .note.GNU-stack,"",@progbits