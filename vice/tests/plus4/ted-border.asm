; Mid-line CSEL/RSEL writes against the TED border flip-flops.
; CASE 0: 40 -> 38 columns in the middle of line 100, back on line 120.
; CASE 1: 25 -> 24 rows in the middle of line 203 (border stays open).
; CASE 2: 24 -> 25 rows in the middle of line 199 (display continues).
; Vertical scroll 0 keeps the tested lines free of DMA, which would stall
; the CPU for most of the line.
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
lda #$71
sta $ff19
ldx #0
clear:
lda #$20
!for p,0,3 { sta $0c00+p*256,x }
inx
bne clear

frame:
lda #250
jsr waitline
lda #$08
sta $ff07
!if CASE = 2 { lda #$10 } else { lda #$18 }
sta $ff06

!if CASE = 0 {
lda #99
jsr waitline
lda #100
jsr waitmid
lda #$00
sta $ff07
lda #120
jsr waitline
lda #$08
sta $ff07
}
!if CASE = 1 {
lda #202
jsr waitline
lda #203
jsr waitmid
lda #$10
sta $ff06
}
!if CASE = 2 {
lda #198
jsr waitline
lda #199
jsr waitmid
lda #$18
sta $ff06
}
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

; Called on the preceding line: catch the start of line A with a short
; loop, then return early in its display window.  With the polling jitter
; and the instructions up to the store, the write lands between cycles 62
; and 80, away from the start (16/18) and stop (94/96) tests.
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
