; CPU writes before/after a displayed bitmap cell. CASE 0 is the control,
; CASE 1 changes RAM before the line, CASE 2 after display, CASE 3 writes twice.
; The complete cell has passed the output well before the late store.
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
sta $ff3f
ldx #0
lda #$ff
fill:
!for p,0,31 { sta $2000+p*256,x }
inx
bne fill
ldx #0
attributes:
lda #7
!for p,0,3 { sta $1800+p*256,x }
lda #$10
!for p,0,3 { sta $1c00+p*256,x }
inx
bne attributes
lda #$18
sta $ff14
!ifdef HIRES { lda #8 }
sta $ff07
lda #8
sta $ff12
lda #$71
sta $ff16
lda #$3b
sta $ff06
frame:
lda $ff1c
and #1
bne frame
lda $ff1d
cmp #101
bne frame
lda #0
!if CASE = 1 { sta $2f02 } else { bit $2f02 }
scanline:
lda $ff1d
cmp #102
bne scanline
left:
bit $ff1e
bmi left
hwait:
lda $ff1e
cmp #$60
bcc hwait
lda #0
!if CASE >= 2 { sta $2f02 } else { bit $2f02 }
!if CASE = 3 {
lda #$55
sta $2f02
}
afterlate:
nextline:
lda $ff1d
cmp #103
bne nextline
lda #$ff
sta $2f02
jmp frame
