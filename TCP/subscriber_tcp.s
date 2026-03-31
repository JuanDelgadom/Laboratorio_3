# subscriber_tcp.s — x86_64 AT&T syntax, Linux
# Compilar: gcc -no-pie -o subscriber_tcp subscriber_tcp.s
# Equivalente a subscriber_tcp.c
 
    .section .data
port:           .long   5000
ip_str:         .string "127.0.0.1"
msg_connected:  .string "Suscriptor conectado...\n"
fmt_received:   .string "Recibido: %s"
 
    .section .bss
sock:           .space 4
serv_addr:      .space 16          # struct sockaddr_in
buffer:         .space 1024
valread:        .space 4
 
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
 
    # ── Configurar serv_addr ──────────────────
    leaq    serv_addr(%rip), %rbx
    movw    $2, (%rbx)            # sin_family = AF_INET
    movw    $0x8813, 2(%rbx)      # sin_port   = htons(5000)
    movl    $0, 4(%rbx)
    movq    $0, 8(%rbx)
 
    # ── inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) ──
    movl    $2, %edi
    leaq    ip_str(%rip), %rsi
    leaq    serv_addr+4(%rip), %rdx
    call    inet_pton
 
    # ── connect(sock, &serv_addr, 16) ─────────
    movl    sock(%rip), %edi
    leaq    serv_addr(%rip), %rsi
    movl    $16, %edx
    call    connect
 
    # ── printf("Suscriptor conectado...\n") ───
    leaq    msg_connected(%rip), %rdi
    xorl    %eax, %eax
    call    printf
 
    movq    stdout(%rip), %rdi
    call    fflush
 
# ═══════════════════════════════════════════
# Loop: recibir mensajes del broker
# ═══════════════════════════════════════════
.loop_recv:
 
    # read(sock, buffer, 1024)
    movl    sock(%rip), %edi
    leaq    buffer(%rip), %rsi
    movl    $1024, %edx
    call    read
    movl    %eax, valread(%rip)
 
    # si valread <= 0 → desconexión
    cmpl    $0, %eax
    jle     .end_subscriber
 
    # buffer[valread] = '\0'
    movslq  valread(%rip), %rcx
    leaq    buffer(%rip), %rax
    movb    $0, (%rax,%rcx,1)
 
    # printf("Recibido: %s", buffer)
    leaq    fmt_received(%rip), %rdi
    leaq    buffer(%rip), %rsi
    xorl    %eax, %eax
    call    printf
 
    movq    stdout(%rip), %rdi
    call    fflush
 
    jmp     .loop_recv
 
.end_subscriber:
    movl    sock(%rip), %edi
    call    close
 
    xorl    %eax, %eax
    popq    %rbp
    ret
 
    .section .note.GNU-stack,"",@progbits
 