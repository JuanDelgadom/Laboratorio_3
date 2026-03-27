    .section .data
port:           .long   5000
ip_str:         .string "127.0.0.1"
msg_connected:  .string "Conectado al broker\n"
 
    .section .bss
sock:           .space 4
serv_addr:      .space 16          # struct sockaddr_in
message:        .space 1024
 
    .section .text
    .globl main
 
main:
    pushq   %rbp
    movq    %rsp, %rbp
 
    # ── socket(AF_INET=2, SOCK_STREAM=1, 0) ──
    movl    $2,  %edi
    movl    $1,  %esi
    xorl    %edx, %edx
    call    socket
    movl    %eax, sock(%rip)
 
    # ── Configurar serv_addr ──────────────────
    leaq    serv_addr(%rip), %rbx
    movw    $2, (%rbx)            # sin_family = AF_INET
    movw    $0x8813, 2(%rbx)      # sin_port   = htons(5000)
    movl    $0, 4(%rbx)           # sin_addr (se llenará con inet_pton)
    movq    $0, 8(%rbx)           # padding
 
    # ── inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) ──
    movl    $2,  %edi
    leaq    ip_str(%rip), %rsi
    leaq    serv_addr+4(%rip), %rdx
    call    inet_pton
 
    # ── connect(sock, &serv_addr, 16) ─────────
    movl    sock(%rip), %edi
    leaq    serv_addr(%rip), %rsi
    movl    $16, %edx
    call    connect
 
    # ── printf("Conectado al broker\n") ────────
    leaq    msg_connected(%rip), %rdi
    xorl    %eax, %eax
    call    printf
 
    movq    stdout(%rip), %rdi
    call    fflush
 
# ═══════════════════════════════════════════
# Loop: leer stdin y enviar al broker
# ═══════════════════════════════════════════
.loop_send:
 
    # fgets(message, 1024, stdin)
    leaq    message(%rip), %rdi
    movl    $1024, %esi
    movq    stdin(%rip), %rdx
    call    fgets
 
    testq   %rax, %rax
    jz      .end_publisher        # EOF → salir
 
    # strlen(message)
    leaq    message(%rip), %rdi
    call    strlen
    movq    %rax, %r12            # r12 = len
 
    # send(sock, message, len, 0)
    movl    sock(%rip), %edi
    leaq    message(%rip), %rsi
    movq    %r12, %rdx
    xorl    %ecx, %ecx
    call    send
 
    jmp     .loop_send
 
.end_publisher:
    movl    sock(%rip), %edi
    call    close
 
    xorl    %eax, %eax
    popq    %rbp
    ret
 
    .section .note.GNU-stack,"",@progbits
 