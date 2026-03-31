# broker_tcp.s — x86_64 AT&T syntax, Linux
# Compilar: gcc -no-pie -o broker_tcp broker_tcp.s
# Equivalente a broker_tcp.c
 
    .section .data
port:           .long   5000
msg_listening:  .string "Broker TCP escuchando en puerto 5000...\n"
msg_new_conn:   .string "Nueva conexión aceptada\n"
msg_received:   .string "Mensaje recibido: %s\n"
 
    .section .bss
server_fd:      .space 4
new_socket:     .space 4
addrlen:        .space 4
client_sockets: .space 40          # int[10]  (4 bytes * 10)
address:        .space 16          # struct sockaddr_in
readfds:        .space 128         # fd_set
buffer:         .space 1024
max_sd:         .space 4
valread:        .space 4
 
    .section .text
    .globl main
 
# ─────────────────────────────────────────────
# Convención de llamada x86_64 System V AMD64:
#   args: rdi, rsi, rdx, rcx, r8, r9
#   retorno: rax
# ─────────────────────────────────────────────
 
main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp
 
    # ── Inicializar client_sockets[] = 0 ──────
    leaq    client_sockets(%rip), %rdi
    xorl    %esi, %esi
    movl    $40, %edx
    call    memset
 
    # ── socket(AF_INET=2, SOCK_STREAM=1, 0) ───
    movl    $2,  %edi
    movl    $1,  %esi
    movl    $0,  %edx
    call    socket
    movl    %eax, server_fd(%rip)
 
    # ── Configurar address ────────────────────
    # sin_family = AF_INET (2)
    leaq    address(%rip), %rbx
    movw    $2, (%rbx)
    # sin_port = htons(5000) = 0x8813
    movw    $0x8813, 2(%rbx)
    # sin_addr = INADDR_ANY = 0
    movl    $0, 4(%rbx)
    # zero padding
    movl    $0, 8(%rbx)
    movl    $0, 12(%rbx)
 
    # addrlen = 16
    movl    $16, addrlen(%rip)
 
    # ── bind(server_fd, &address, 16) ─────────
    movl    server_fd(%rip), %edi
    leaq    address(%rip), %rsi
    movl    $16, %edx
    call    bind
 
    # ── listen(server_fd, 5) ──────────────────
    movl    server_fd(%rip), %edi
    movl    $5, %esi
    call    listen
 
    # ── printf("Broker TCP escuchando...") ────
    leaq    msg_listening(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
 
# ═══════════════════════════════════════════
# Loop principal
# ═══════════════════════════════════════════
.loop_main:
 
    # ── FD_ZERO(&readfds) ─────────────────────
    leaq    readfds(%rip), %rdi
    xorl    %esi, %esi
    movl    $128, %edx
    call    memset
 
    # ── FD_SET(server_fd, &readfds) ───────────
    movl    server_fd(%rip), %eax
    movl    %eax, max_sd(%rip)
    call    fdset_server          # macro inline abajo
 
    # ── Añadir clientes al set ────────────────
    xorl    %r12d, %r12d          # i = 0
.loop_add_clients:
    cmpl    $10, %r12d
    jge     .done_add_clients
 
    # sd = client_sockets[i]
    leaq    client_sockets(%rip), %rax
    movslq  %r12d, %rcx
    movl    (%rax,%rcx,4), %r13d  # r13d = sd
 
    testl   %r13d, %r13d
    jle     .next_add             # sd <= 0 → skip
 
    # FD_SET(sd, &readfds)
    movl    %r13d, %edi
    leaq    readfds(%rip), %rsi
    call    __fd_set_manual
 
    # if (sd > max_sd) max_sd = sd
    movl    max_sd(%rip), %eax
    cmpl    %eax, %r13d
    jle     .next_add
    movl    %r13d, max_sd(%rip)
 
.next_add:
    incl    %r12d
    jmp     .loop_add_clients
 
.done_add_clients:
 
    # ── select(max_sd+1, &readfds, NULL, NULL, NULL) ──
    movl    max_sd(%rip), %edi
    incl    %edi
    leaq    readfds(%rip), %rsi
    xorl    %edx, %edx
    xorl    %ecx, %ecx
    xorl    %r8d, %r8d
    call    select
 
    # ── ¿Nueva conexión? FD_ISSET(server_fd, &readfds) ──
    movl    server_fd(%rip), %edi
    leaq    readfds(%rip), %rsi
    call    __fd_isset_manual
    testl   %eax, %eax
    jz      .handle_clients
 
    # accept(server_fd, &address, &addrlen)
    movl    server_fd(%rip), %edi
    leaq    address(%rip), %rsi
    leaq    addrlen(%rip), %rdx
    call    accept
    movl    %eax, new_socket(%rip)
 
    leaq    msg_new_conn(%rip), %rdi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
 
    # Guardar en primer slot libre
    xorl    %r12d, %r12d
.loop_store:
    cmpl    $10, %r12d
    jge     .handle_clients
 
    leaq    client_sockets(%rip), %rax
    movslq  %r12d, %rcx
    movl    (%rax,%rcx,4), %r13d
    testl   %r13d, %r13d
    jnz     .next_store
 
    movl    new_socket(%rip), %r13d
    movl    %r13d, (%rax,%rcx,4)
    jmp     .handle_clients
 
.next_store:
    incl    %r12d
    jmp     .loop_store
 
# ── Manejar datos de clientes ─────────────
.handle_clients:
    xorl    %r12d, %r12d          # i = 0
.loop_clients:
    cmpl    $10, %r12d
    jge     .loop_main
 
    leaq    client_sockets(%rip), %rax
    movslq  %r12d, %rcx
    movl    (%rax,%rcx,4), %r13d  # r13d = sd
 
    testl   %r13d, %r13d
    jle     .next_client
 
    # FD_ISSET(sd, &readfds)
    movl    %r13d, %edi
    leaq    readfds(%rip), %rsi
    call    __fd_isset_manual
    testl   %eax, %eax
    jz      .next_client
 
    # valread = read(sd, buffer, 1024)
    movl    %r13d, %edi
    leaq    buffer(%rip), %rsi
    movl    $1024, %edx
    call    read
    movl    %eax, valread(%rip)
 
    cmpl    $0, %eax
    jne     .got_data
 
    # valread == 0 → cliente desconectado
    movl    %r13d, %edi
    call    close
 
    leaq    client_sockets(%rip), %rax
    movslq  %r12d, %rcx
    movl    $0, (%rax,%rcx,4)
    jmp     .next_client
 
.got_data:
    # buffer[valread] = '\0'
    movslq  valread(%rip), %rcx
    leaq    buffer(%rip), %rax
    movb    $0, (%rax,%rcx,1)
 
    # printf("Mensaje recibido: %s\n", buffer)
    leaq    msg_received(%rip), %rdi
    leaq    buffer(%rip), %rsi
    xorl    %eax, %eax
    call    printf
    call    fflush_stdout
 
    # Broadcast a todos los demás
    xorl    %r14d, %r14d          # j = 0
.loop_broadcast:
    cmpl    $10, %r14d
    jge     .next_client
 
    leaq    client_sockets(%rip), %rax
    movslq  %r14d, %rcx
    movl    (%rax,%rcx,4), %r15d  # r15d = client_sockets[j]
 
    testl   %r15d, %r15d
    jz      .next_broadcast       # == 0 → skip
 
    cmpl    %r13d, %r15d
    je      .next_broadcast       # == sd → no enviarse a sí mismo
 
    # send(client_sockets[j], buffer, strlen(buffer), 0)
    movl    %r15d, %edi
    leaq    buffer(%rip), %rsi
    leaq    buffer(%rip), %rdi
    call    strlen
    movq    %rax, %rdx            # len
    movl    %r15d, %edi
    leaq    buffer(%rip), %rsi
    xorl    %ecx, %ecx
    call    send
 
.next_broadcast:
    incl    %r14d
    jmp     .loop_broadcast
 
.next_client:
    incl    %r12d
    jmp     .loop_clients
 
# ═══════════════════════════════════════════
# Helpers: FD_SET / FD_ISSET manuales
# fd_set en Linux = 128 bytes = 1024 bits
# FD_SET(fd, set)  → set[fd/64] |= (1 << (fd%64))
# FD_ISSET(fd, set)→ set[fd/64] &  (1 << (fd%64))  ≠ 0
# ═══════════════════════════════════════════
 
# void __fd_set_manual(int fd /*edi*/, fd_set* set /*rsi*/)
__fd_set_manual:
    pushq   %rbp
    movq    %rsp, %rbp
    movl    %edi, %eax          # fd
    movl    %eax, %ecx
    shrl    $6, %ecx            # word_index = fd / 64
    andl    $63, %eax           # bit_index  = fd % 64
    movslq  %ecx, %rcx
    movq    $1, %rdx
    shlq    %cl, %rdx           # mask = 1 << bit_index   (cl = al del and)
    # Cuidado: cl debe venir de al (bit_index)
    movl    %eax, %ecx          # cl = bit_index
    movq    $1, %rdx
    shlq    %cl, %rdx
    movslq  (%rip), %rcx        # dummy — recalcular word_index
    movl    %edi, %eax
    shrl    $6, %eax
    movslq  %eax, %rcx
    orq     %rdx, (%rsi,%rcx,8)
    popq    %rbp
    ret
 
# int __fd_isset_manual(int fd /*edi*/, fd_set* set /*rsi*/) → eax
__fd_isset_manual:
    pushq   %rbp
    movq    %rsp, %rbp
    movl    %edi, %eax
    movl    %eax, %r8d
    shrl    $6, %r8d            # word_index
    andl    $63, %eax           # bit_index
    movl    %eax, %ecx
    movq    $1, %rdx
    shlq    %cl, %rdx           # mask
    movslq  %r8d, %r8
    testq   %rdx, (%rsi,%r8,8)
    setnz   %al
    movzbl  %al, %eax
    popq    %rbp
    ret
 
fdset_server:
    # FD_SET(server_fd, &readfds)
    movl    server_fd(%rip), %edi
    leaq    readfds(%rip), %rsi
    jmp     __fd_set_manual
 
fflush_stdout:
    pushq   %rbp
    movq    %rsp, %rbp
    movq    stdout(%rip), %rdi
    call    fflush
    popq    %rbp
    ret
 
    .section .note.GNU-stack,"",@progbits