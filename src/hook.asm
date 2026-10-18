; SnowRunner no-follow camera hook v2 (position independent blob)
; Header/data at +0, code entry at +96
bits 64
default rel

blob:
retAddr:  dq 0              ; +0  filled by loader (target+8)
magic:    db 'SRNFCAM2'     ; +8
enabled:  db 1              ; +16
primed:   db 0              ; +17
hold:     db 0              ; +18 frames left to pause correction after stick use
          db 0              ; +19
lastH:    dd 0              ; +20
prevOff:  dd 0              ; +24
tmp:      dd 0              ; +28
lastObj:  dq 0              ; +32
gain:     dd 1.0            ; +40
pi:       dd 3.14159265     ; +44
negPi:    dd -3.14159265    ; +48
twoPi:    dd 6.28318531     ; +52
zero:     dd 0.0            ; +56
eps:      dd 0.000001       ; +60
negEps:   dd -0.000001      ; +64
          times 96-($-blob) db 0
; ---- code entry: +96 ----
entry:
  pushfq
  sub rsp,0x20
  movdqu [rsp],xmm1
  movdqu [rsp+0x10],xmm2
  cmp [lastObj],rbx
  jne .reset
  ; right-stick movement this frame = new offset - stored offset
  movss xmm2,xmm0
  subss xmm2,[rbx+0x1E8]
  comiss xmm2,[eps]
  ja .stick
  comiss xmm2,[negEps]
  jae .noStick
.stick:
  mov byte [hold],4         ; pause correction for 4 frames after stick input
.noStick:
  ; truck-follow angle H = atan2(eye.z-tgt.z, eye.x-tgt.x) + offset used for that frame
  fld dword [rbx+0x174]
  fsub dword [rbx+0x180]
  fld dword [rbx+0x16C]
  fsub dword [rbx+0x178]
  fpatan
  fadd dword [prevOff]
  fstp dword [tmp]
  movss xmm1,[tmp]
  ucomiss xmm1,xmm1
  jp .savePrev
  cmp byte [primed],0
  je .prime
  cmp byte [hold],0
  je .noHold
  dec byte [hold]           ; stick in use: leave the camera to the player
  jmp .store
.noHold:
  cmp byte [enabled],0
  je .store
  movss xmm2,xmm1
  subss xmm2,[lastH]
  comiss xmm2,[pi]
  jbe .a
  subss xmm2,[twoPi]
.a:
  comiss xmm2,[negPi]
  jae .b
  addss xmm2,[twoPi]
.b:
  mulss xmm2,[gain]
  addss xmm0,xmm2
  comiss xmm0,[twoPi]
  jb .c
  subss xmm0,[twoPi]
.c:
  comiss xmm0,[zero]
  jae .store
  addss xmm0,[twoPi]
.store:
  movss [lastH],xmm1
  jmp .savePrev
.prime:
  movss [lastH],xmm1
  mov byte [primed],1
  jmp .savePrev
.reset:
  mov [lastObj],rbx
  mov byte [primed],0
.savePrev:
  movss xmm1,[rbx+0x1E8]
  movss [prevOff],xmm1
  movdqu xmm1,[rsp]
  movdqu xmm2,[rsp+0x10]
  add rsp,0x20
  popfq
  movss [rbx+0x1E8],xmm0      ; original instruction
  jmp [retAddr]
