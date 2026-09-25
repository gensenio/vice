; A horizontal scroll write in the middle of line 100 moves the characters
; that follow it on the same line.  Alternating reverse spaces and spaces
; draw 16 pixel blocks; vertical scroll 0 keeps line 100 free of DMA.
!cpu 6502
* = $1001
!word endbasic
!word 10
!byte $9e
!text "4110"
!byte 0
endbasic: !word 0
* = $100e
sei
lda #0
sta $ff0a
sta $ff15
sta $ff19
ldx #0
fill:
txa
and #1
beq solid
lda #$20
!byte $2c
solid:
lda #$a0
!for p,0,3 { sta $0c00+p*256,x }
lda #$71
!for p,0,3 { sta $0800+p*256,x }
inx
bne fill
lda #$18
sta $ff06

frame:
lda #250
jsr waitline
lda #$08
sta $ff07
lda #99
jsr waitline
lda #100
jsr waitmid
lda #$0c
sta $ff07
jmp frame

waitline:
sta line
wait:
lda $ff1c
and #1
bne wait
lda $ff1d
cmp line
bne wait
rts

; Called on the preceding line: catch the start of line A, then return
; early in its display window (the store lands between cycles 62 and 80).
waitmid:
cmp $ff1d
bne waitmid
early:
lda $ff1e
cmp #$10
bcc early
cmp #$40
bcs early
rts

line: !byte 0
