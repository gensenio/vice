; DEN set during raster line zero must enable the display CPU clock window.
; CASE 0 enables DEN before the frame; CASE 1 enables it during line zero.
; Sample a normal line (neither attribute nor character matrix DMA).
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
sta $ff13
sta $ff3f
sta $ff06
waitborder:
lda $ff1c
and #1
beq waitborder
lda $ff1d
cmp #44
bne waitborder
!if CASE = 0 {
    lda #$1b
    sta $ff06
}
waitframe:
lda $ff1c
and #1
bne waitframe
!if CASE = 1 {
    lda #$1b
    sta $ff06
}
waitline:
lda $ff1d
cmp #16
bne waitline
lda $ff1e
sta $1800
!for i,1,8 { nop }
lda $ff1e
sta $1801
done:
jmp done
