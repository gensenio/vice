; Idle fetches read $ffff with the ROM/RAM selection of the DMA fetches.
; The lower border is opened (25 -> 24 rows on line 203) to show them.
; RAM $ffff holds $81; the Kernal ROM has $fc there.
; CASE 0: ROM selected (default).  CASE 1: RAM selected with $ff3f.
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
sta $ff19
lda #$71
sta $ff15
lda #$81
sta $ffff
!if CASE = 1 { sta $ff3f }

frame:
lda #250
jsr waitline
lda #$08
sta $ff07
lda #$18
sta $ff06
lda #202
jsr waitline
lda #203
line203:
cmp $ff1d
bne line203
lda #$10
sta $ff06
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
