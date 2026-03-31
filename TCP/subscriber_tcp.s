.section .data
port:           .word   0x1F90              # 8080 en network byte order
ip_str:         .string "127.0.0.1"

msg_connected:  .string "[SUB] Suscrito. Esperando...\n"
fmt_received:   .string "[SUB] %s\n"

default_topic:  .string "Colombia vs Brasil"
sub_prefix:     .string "SUB %s\n"

.section .bss
sock:           .space 4
serv_addr:      .space 16
buffer:         .space 1024
valread:        .space 4
header:         .space 256

.section .text
.globl main

main:
    pushq   %rbp
    movq    %rsp, %rbp

    # ───────────────────────────────
    # socket(AF_INET, SOCK_STREAM, 0)
    # ───────────────────────────────
    movl    $2, %edi
    movl    $1, %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sock(%rip)

    # ───────────────────────────────
    # struct sockaddr_in
    # ───────────────────────────────
    leaq    serv_addr(%rip), %rbx
    movw    $2, (%rbx)                 # AF_INET
    movw    port(%rip), %ax
    movw    %ax, 2(%rbx)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)

    # inet_pton
    movl    $2, %edi
    leaq    ip_str(%rip), %rsi
    leaq    serv_addr+4(%rip), %rdx
    call    inet_pton

    # ───────────────────────────────
    # connect()
    # ───────────────────────────────
    movl    sock(%rip), %edi
    leaq    serv_addr(%rip), %rsi
    movl    $16, %edx
    call    connect

    # ───────────────────────────────
    # snprintf(header, "SUB %s\n")
    # ───────────────────────────────
    leaq    header(%rip), %rdi         # dest
    movl    $256, %esi                # size
    leaq    sub_prefix(%rip), %rdx    # format
    leaq    default_topic(%rip), %rcx # topic
    call    snprintf

    # ───────────────────────────────
    # send(fd, header, strlen(header), 0)
    # ───────────────────────────────
    leaq    header(%rip), %rdi
    call    strlen
    movq    %rax, %rdx                # length

    movl    sock(%rip), %edi
    leaq    header(%rip), %rsi
    xorl    %ecx, %ecx
    call    send

    # ───────────────────────────────
    # printf conectado
    # ───────────────────────────────
    leaq    msg_connected(%rip), %rdi
    xorl    %eax, %eax
    call    printf

# ═══════════════════════════════════════
# LOOP DE RECEPCIÓN (igual al C)
# ═══════════════════════════════════════

recv_loop:

    # read(sock, buffer, 1024)
    movl    sock(%rip), %edi
    leaq    buffer(%rip), %rsi
    movl    $1024, %edx
    call    read
    movl    %eax, valread(%rip)

    # if (r <= 0) break;
    cmpl    $0, %eax
    jle     end_subscriber

    # buffer[r] = '\0'
    movslq  valread(%rip), %rcx
    leaq    buffer(%rip), %rax
    movb    $0, (%rax,%rcx)

    # printf("[SUB] %s\n", buffer)
    leaq    fmt_received(%rip), %rdi
    leaq    buffer(%rip), %rsi
    xorl    %eax, %eax
    call    printf

    jmp     recv_loop

# ───────────────────────────────
# close()
# ───────────────────────────────
end_subscriber:
    movl    sock(%rip), %edi
    call    close

    xorl    %eax, %eax
    popq    %rbp
    ret

.section .note.GNU-stack,"",@progbits