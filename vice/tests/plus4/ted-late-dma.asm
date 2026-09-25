; A vertical scroll write in the middle of line 100 makes it an attribute
; DMA line.  Characters already passed keep the attributes fetched on line
; 96 ($0800, colour A); three characters receive the operand after the
; store ($55); the rest receive the new attributes ($1800, colour B).
; $1f00/$1f01 record the horizontal counter before and after the store.
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
lda #$a0
!for p,0,3 { sta $0c00+p*256,x }
!for p,0,3 { sta $1c00+p*256,x }
lda #$71
!for p,0,3 { sta $0800+p*256,x }
lda #$32
!for p,0,3 { sta $1800+p*256,x }
inx
bne fill

frame:
lda #250
jsr waitline
lda #$18
sta $ff06
lda #$08
sta $ff14
lda #98
jsr waitline
lda #$18
sta $ff14
lda #99
jsr waitline
lda #100
line100:
cmp $ff1d
bne line100
; Leave the right border: the store lands between cycles 36 and 56.
right:
bit $ff1e
bmi right
ldx $ff1e
lda #$1c
sta $ff06
lda #$55
ldy $ff1e
stx $1f00
sty $1f01
done:
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

line: !byte 0
