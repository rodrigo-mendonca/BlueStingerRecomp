; Teste de preservacao de XMM6-XMM15 atraves de trocas de fibra (tests/unit/test_threads.cpp).
; Na ABI x64 do Windows, XMM6-XMM15 sao preservados entre chamadas; uma fibra suspensa
; dentro de uma chamada pode ter valores vivos neles.
;
; void xmm_probe(const double* in, double* out, void (*cb)(void*), void* arg)
;   salva os XMM6-XMM15 do chamador, carrega in[0..9] em XMM6-XMM15, chama cb(arg),
;   grava XMM6-XMM15 em out[0..9] e restaura os registradores do chamador.

.code

xmm_probe PROC FRAME
    push rbx
    .PUSHREG rbx
    sub rsp, 192
    .ALLOCSTACK 192
    movdqa [rsp+32], xmm6
    .SAVEXMM128 xmm6, 32
    movdqa [rsp+48], xmm7
    .SAVEXMM128 xmm7, 48
    movdqa [rsp+64], xmm8
    .SAVEXMM128 xmm8, 64
    movdqa [rsp+80], xmm9
    .SAVEXMM128 xmm9, 80
    movdqa [rsp+96], xmm10
    .SAVEXMM128 xmm10, 96
    movdqa [rsp+112], xmm11
    .SAVEXMM128 xmm11, 112
    movdqa [rsp+128], xmm12
    .SAVEXMM128 xmm12, 128
    movdqa [rsp+144], xmm13
    .SAVEXMM128 xmm13, 144
    movdqa [rsp+160], xmm14
    .SAVEXMM128 xmm14, 160
    movdqa [rsp+176], xmm15
    .SAVEXMM128 xmm15, 176
    .ENDPROLOG

    mov rbx, rdx                ; out (rbx e preservado pelo callback)
    movsd xmm6,  QWORD PTR [rcx]
    movsd xmm7,  QWORD PTR [rcx+8]
    movsd xmm8,  QWORD PTR [rcx+16]
    movsd xmm9,  QWORD PTR [rcx+24]
    movsd xmm10, QWORD PTR [rcx+32]
    movsd xmm11, QWORD PTR [rcx+40]
    movsd xmm12, QWORD PTR [rcx+48]
    movsd xmm13, QWORD PTR [rcx+56]
    movsd xmm14, QWORD PTR [rcx+64]
    movsd xmm15, QWORD PTR [rcx+72]
    mov rcx, r9
    call r8
    movsd QWORD PTR [rbx],    xmm6
    movsd QWORD PTR [rbx+8],  xmm7
    movsd QWORD PTR [rbx+16], xmm8
    movsd QWORD PTR [rbx+24], xmm9
    movsd QWORD PTR [rbx+32], xmm10
    movsd QWORD PTR [rbx+40], xmm11
    movsd QWORD PTR [rbx+48], xmm12
    movsd QWORD PTR [rbx+56], xmm13
    movsd QWORD PTR [rbx+64], xmm14
    movsd QWORD PTR [rbx+72], xmm15

    movdqa xmm6,  [rsp+32]
    movdqa xmm7,  [rsp+48]
    movdqa xmm8,  [rsp+64]
    movdqa xmm9,  [rsp+80]
    movdqa xmm10, [rsp+96]
    movdqa xmm11, [rsp+112]
    movdqa xmm12, [rsp+128]
    movdqa xmm13, [rsp+144]
    movdqa xmm14, [rsp+160]
    movdqa xmm15, [rsp+176]
    add rsp, 192
    pop rbx
    ret
xmm_probe ENDP

END
