; $Id: DisasmTestA.asm $
;; @file
; VBox disassembler: Assembler test routines
;

;
; Copyright (C) 2006-2010 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;

;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "VBox/nasm.mac"
%include "VBox/vmm/vm.mac"
%include "VBox/err.mac"
%include "VBox/vmm/stam.mac"
%include "iprt/x86.mac"

BITS 32

BEGINCODE

align 16
BEGINPROC   TestProc
    xor eax, eax
    mov al, 4
    lea edx, [4]
    mov edx, 4
    mov eax, 4
    shl eax, 4
    shl edx, 4
    shr edx, 4
    mov eax, edx
    mov eax, ecx
    mov edx, eax
    mov ecx, eax
    DB 0xF0, 0x0F, 0x22, 0xC0
    DB 0xF0, 0x0F, 0x20, 0xC0
    smsw  word [edx+16]
;    invept      eax, qword [ecx]
    DB          0x66, 0x0F, 0x38, 0x80, 0x1
;    invept      eax, qword [ecx]
    DB          0x66, 0x0F, 0x38, 0x81, 0x1
      mov   eax, dword [ecx]
      mov   word [edi], 0123ah
      movzx eax,byte  [edx]
      movzx eax,word  [edx]
;      mov dword es:[ebx + 1234h], 0789h
;      mov word  fs:[ebx + ecx], 0654h
;      mov byte  [esi + eax*4], 0654h
;      mov bl, byte  ds:[ebp + 1234h]
;      mov al, cs:[1234h + ecx*8]
;      mov al, cs:[1234h]
;      mov ax, cs:[1234h]
;      mov eax, cs:[1234h]
      lock cmpxchg [ecx], eax
      lock cmpxchg [ecx], ax
      lock cmpxchg [ecx], dl
      movzx ESI,word  [EAX]
      in al, dx
      in ax, dx
      in eax, dx
      mov ebx, [ecx + eax*4 + 17]
      mov ebx, [ebp + eax*4 + 4]
      mov ebx, [ebp + eax*4]
      int 80h
      in  al, 60h
      in  ax, dx
      out 64h, eax

      movss xmm0, xmm1
      movsd xmm6, xmm1

      pause

ENDPROC   TestProc

%ifndef RT_OS_OS2
BITS 64
align 16
BEGINPROC TestProc64
    mov cr8, rax
    mov cr8, rbx
    mov [0xfffe0080], rax
    mov [0xfffe0080], rbx
    mov rax, cr8
    mov rbx, cr8
    mov rax, [0xfffe0080]
    mov rbx, [0xfffe0080]
    divsd xmm1, xmm0
 ;    invept      rdi, qword [rsi]
    DB          0x66, 0x0F, 0x38, 0x80, 0x3E
;    invept      rcx, qword [rdx]
    DB          0x66, 0x0F, 0x38, 0x80, 0xA
    ;invvpid     rdi, qword [rsi]
    DB          0x66, 0x0F, 0x38, 0x81, 0x3E
;    invvpid     rcx, qword [rdx]
    DB          0x66, 0x0F, 0x38, 0x81, 0xA
      mov   rdi, [rsi]
      mov   rcx, [rdx]
      db 48h
      db 0c7h
      db 42h
      db 18h
      db 20h
      db 3eh
      db 23h
      db 80h
      call qword [r8+10h]
      ; test
      db 48h
      db 8bh
      db 44h
      db 0ah
      db 0f8h
      ;incorrectly assembled by yasm; REX.W should not be added!
      ;test rax, dword 0cc90cc90h
      db 8bh
      db 04h
      db 8dh
      db 00h
      db 00h
      db 0feh
      db 0ffh
      mov   qword [rcx+rdx], 0
      mov   dword [rcx+rdx], 0
      and   [r15], rax
      movzx rcx, sil
      and   sil, 3
      movzx ecx, ah
      and   ah, 3

      sub rcx, 1234h
      mov rax, qword [0cc90cc90h]
      mov rax, qword [00c90cc90h]
      mov rax, dword 0cc90cc90h
      mov rax, qword 0ffffcc90cc90h

      movzx rax,byte  [edx]
      movzx rax,word  [edx]
      movzx rax,byte  [rdx]
      lock cmpxchg [rcx], rax
      lock cmpxchg [rcx], ax
      lock cmpxchg [r15], dl
      movzx RSI, word [R8]
      in al, dx
      in ax, dx
      in eax, dx
      mov rbx, [rcx + rax*4 + 17]
      mov rbx, [rbp + rax*4 + 4]
      mov rbx, [rbp + rax*4]
      mov rbx, [ebp + eax*4]
      int 80h
      in  al, 60h
      in  ax, dx
      out 64h, eax

      movss xmm0, xmm14
      movsd xmm6, xmm1

      ret
ENDPROC   TestProc64
%endif