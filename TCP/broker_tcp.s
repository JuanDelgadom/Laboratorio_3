.section .data
port:           .word   0x1F90   # 8080
msg_listening:  .string "[BROKER] Escuchando en 8080...\n"
msg_new_conn:   .string "[BROKER] Nueva conexion\n"

str_pub: .string "PUB"
str_sub: .string "SUB"

.section .bss
server_fd:      .space 4
new_socket:     .space 4
addrlen:        .space 4
client_sockets: .space 40        # 10 clientes
topics:         .space 1280      # 10 * 128 bytes
address:        .space 16
readfds:        .space 128
buffer:         .space 1024
max_sd:         .space 4
valread:        .space 4

.section .text
.globl main

main:
    pushq %rbp
    movq %rsp, %rbp

    # memset(client_sockets, 0, 40)
    leaq client_sockets(%rip), %rdi
    xorl %esi, %esi
    movl $40, %edx
    call memset

    # socket(AF_INET, SOCK_STREAM, 0)
    movl $2, %edi
    movl $1, %esi
    xorl %edx, %edx
    call socket
    movl %eax, server_fd(%rip)

    # configurar sockaddr
    leaq address(%rip), %rbx
    movw $2, (%rbx)
    movw port(%rip), %ax
    movw %ax, 2(%rbx)
    movl $0, 4(%rbx)
    movq $0, 8(%rbx)

    movl $16, addrlen(%rip)

    # bind
    movl server_fd(%rip), %edi
    leaq address(%rip), %rsi
    movl $16, %edx
    call bind

    # listen
    movl server_fd(%rip), %edi
    movl $10, %esi
    call listen

    # printf listening
    leaq msg_listening(%rip), %rdi
    xorl %eax, %eax
    call printf

# ───────────────── LOOP PRINCIPAL ─────────────────
loop_main:

    # FD_ZERO
    leaq readfds(%rip), %rdi
    xorl %esi, %esi
    movl $128, %edx
    call memset

    # agregar server_fd
    movl server_fd(%rip), %edi
    leaq readfds(%rip), %rsi
    call __fd_set_manual

    movl server_fd(%rip), %eax
    movl %eax, max_sd(%rip)

    # agregar clientes
    xorl %r12d, %r12d
add_clients:
    cmpl $10, %r12d
    jge done_add

    leaq client_sockets(%rip), %rax
    movslq %r12d, %rcx
    movl (%rax,%rcx,4), %r13d

    testl %r13d, %r13d
    jle next_add

    movl %r13d, %edi
    leaq readfds(%rip), %rsi
    call __fd_set_manual

    movl max_sd(%rip), %eax
    cmpl %eax, %r13d
    jle next_add
    movl %r13d, max_sd(%rip)

next_add:
    incl %r12d
    jmp add_clients

done_add:

    # select
    movl max_sd(%rip), %edi
    incl %edi
    leaq readfds(%rip), %rsi
    xorl %edx, %edx
    xorl %ecx, %ecx
    xorl %r8d, %r8d
    call select

    # nueva conexion?
    movl server_fd(%rip), %edi
    leaq readfds(%rip), %rsi
    call __fd_isset_manual
    testl %eax, %eax
    jz handle_clients

    # accept
    movl server_fd(%rip), %edi
    leaq address(%rip), %rsi
    leaq addrlen(%rip), %rdx
    call accept
    movl %eax, new_socket(%rip)

    leaq msg_new_conn(%rip), %rdi
    xorl %eax, %eax
    call printf

    # guardar cliente
    xorl %r12d, %r12d
store_loop:
    cmpl $10, %r12d
    jge handle_clients

    leaq client_sockets(%rip), %rax
    movslq %r12d, %rcx
    movl (%rax,%rcx,4), %r13d
    testl %r13d, %r13d
    jnz next_store

    movl new_socket(%rip), %r13d
    movl %r13d, (%rax,%rcx,4)
    jmp handle_clients

next_store:
    incl %r12d
    jmp store_loop

# ───────────── manejar clientes ─────────────
handle_clients:
    xorl %r12d, %r12d

loop_clients:
    cmpl $10, %r12d
    jge loop_main

    leaq client_sockets(%rip), %rax
    movslq %r12d, %rcx
    movl (%rax,%rcx,4), %r13d

    testl %r13d, %r13d
    jle next_client

    movl %r13d, %edi
    leaq readfds(%rip), %rsi
    call __fd_isset_manual
    testl %eax, %eax
    jz next_client

    # read
    movl %r13d, %edi
    leaq buffer(%rip), %rsi
    movl $1024, %edx
    call read
    movl %eax, valread(%rip)

    cmpl $0, %eax
    jne got_data

    # desconexion
    movl %r13d, %edi
    call close
    movl $0, (%rax,%rcx,4)
    jmp next_client

got_data:
    movslq valread(%rip), %rcx
    leaq buffer(%rip), %rax
    movb $0, (%rax,%rcx)

    # ─── detectar SUB ───
    leaq buffer(%rip), %rdi
    leaq str_sub(%rip), %rsi
    call strncmp
    cmpl $0, %eax
    je is_sub

    # ─── detectar PUB ───
    leaq buffer(%rip), %rdi
    leaq str_pub(%rip), %rsi
    call strncmp
    cmpl $0, %eax
    je is_pub

    jmp next_client

# guardar topic
is_sub:
    leaq topics(%rip), %rax
    movslq %r12d, %rcx
    imulq $128, %rcx, %rcx
    addq %rcx, %rax

    leaq buffer(%rip), %rsi
    movq %rax, %rdi
    movl $128, %edx
    call strncpy

    jmp next_client

# broadcast filtrado
is_pub:
    xorl %r14d, %r14d

broadcast_loop:
    cmpl $10, %r14d
    jge next_client

    leaq client_sockets(%rip), %rax
    movslq %r14d, %rcx
    movl (%rax,%rcx,4), %r15d

    testl %r15d, %r15d
    jz next_broadcast

    # comparar topic
    leaq topics(%rip), %rax
    movslq %r14d, %rcx
    imulq $128, %rcx, %rcx
    addq %rcx, %rax

    movq %rax, %rdi
    leaq buffer(%rip), %rsi
    call strcmp
    cmpl $0, %eax
    jne next_broadcast

    # send
    movl %r15d, %edi
    leaq buffer(%rip), %rsi
    call strlen
    movq %rax, %rdx
    movl %r15d, %edi
    leaq buffer(%rip), %rsi
    xorl %ecx, %ecx
    call send

next_broadcast:
    incl %r14d
    jmp broadcast_loop

next_client:
    incl %r12d
    jmp loop_clients


# ───────── helpers ─────────

__fd_set_manual:
    movl %edi, %eax
    movl %eax, %ecx
    shrl $6, %ecx
    andl $63, %eax
    movl %eax, %ecx
    movq $1, %rdx
    shlq %cl, %rdx
    movl %edi, %eax
    shrl $6, %eax
    movslq %eax, %rcx
    orq %rdx, (%rsi,%rcx,8)
    ret

__fd_isset_manual:
    movl %edi, %eax
    movl %eax, %r8d
    shrl $6, %r8d
    andl $63, %eax
    movl %eax, %ecx
    movq $1, %rdx
    shlq %cl, %rdx
    movslq %r8d, %r8
    testq %rdx, (%rsi,%r8,8)
    setnz %al
    movzbl %al, %eax
    ret

.section .note.GNU-stack,"",@progbits