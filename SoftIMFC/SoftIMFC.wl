name SoftIMFC

file SoftIMFC
file IMFC
file MPU401
file DBS2P
file res_imfc
file res_glue
file res_end

system dos

order
  clname CODE
    segment BEGTEXT
    segment RESIDENT
    segment RESEND
    segment _TEXT
  clname FAR_DATA
  clname BEGDATA
  clname DATA
  clname BSS noemit
  clname STACK noemit

option map
option quiet
