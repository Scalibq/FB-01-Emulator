        public _amis_header
        public _amis_id
        public _amis_handler

        public _emm386_table
        public _qemm_handler

        extern _config : near
        extern emulate_imfc_io_ : proc


cmp_ah  macro
        db 0x80, 0xFC
        endm


        RESIDENT segment word use16 public 'CODE'


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; AMIS API IMPLEMENTATION


_amis_header:
        db 'SCALI   '           ;8 bytes: manufacturer
        db 'SOFTIMFC'           ;8 bytes: product
        db 0                    ;no description
;;; Configuration pointer immediately follows AMIS header
        dw _config


;;; IBM Interrupt Sharing Protocol header
iisp_header macro chain
        jmp short $+0x12
chain:  dd 0
        dw 0x424B               ;signature
        db 0                    ;flags
        jmp short _retf         ;hardware reset routine
        db 7 dup (0)            ;unused/zero
        endm


_amis_handler:
        iisp_header amis_next_handler
        cmp_ah
_amis_id: db 0xFF
        je @@amis_match
        jmp dword ptr cs:amis_next_handler
@@amis_match:
        test al, al
        je @@amis_install_check
        cmp al, 4
        je @@amis_hook_table
        xor al, al
        iret
@@amis_install_check:
        mov al, 0xFF
        mov cx, (VERSION_MAJOR * 256 + VERSION_MINOR)
        mov dx, cs
        mov di, _amis_header
        iret
@@amis_hook_table:
        mov dx, cs
        mov bx, amis_hook_table
        iret

amis_hook_table:
        db 0x2D
        dw _amis_handler


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; EMM386 GLUE CODE


        even
_emm386_table:
        dw 0x2A20, emm386_handler
        dw 0x2A21, emm386_handler
        dw 0x2A22, emm386_handler
        dw 0x2A23, emm386_handler
        dw 0x2A24, emm386_handler
        dw 0x2A25, emm386_handler
        dw 0x2A26, emm386_handler
        dw 0x2A27, emm386_handler
        dw 0x2A28, emm386_handler
        dw 0x2A29, emm386_handler
        dw 0x2A2A, emm386_handler
        dw 0x2A2B, emm386_handler
        dw 0x2A2C, emm386_handler
        dw 0x2A2D, emm386_handler
        dw 0x2A2E, emm386_handler
        dw 0x2A2F, emm386_handler

emm386_handler:
        call emulate_imfc_io_
        clc
_retf:  retf


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; QEMM GLUE CODE


_qemm_handler:
        iisp_header qemm_next_handler
        cmp dx, 0x2A20
        jl @@qemm_ignore
        cmp dx, 0x2A2F
        jg @@qemm_ignore
        and cx, 4
        push ds
        push cs
        pop ds
        call emulate_imfc_io_
        pop ds
        retf
@@qemm_ignore:
        jmp dword ptr cs:qemm_next_handler


        RESIDENT ends
        end
