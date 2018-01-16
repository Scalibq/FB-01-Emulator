@echo off
SET VERSION_MAJOR=0
SET VERSION_MINOR=1

SET CC=wcc -bt=dos -zq -oxhs
SET CC32=wcc386 -mf -zl -zls -zq -oxhs
SET AS=wasm -zq
SET DEFS=-dVERSION_MAJOR=%VERSION_MAJOR% -dVERSION_MINOR=%VERSION_MINOR%

%CC% %DEFS% SoftIMFC.c
%CC% %DEFS% IMFC.c
%CC% %DEFS% DBS2P.c
%CC% %DEFS% MPU401.c
%CC% %DEFS% res_imfc.c
%AS% %DEFS% res_glue.asm
%AS% %DEFS% res_end.asm
wlink @softimfc.wl
