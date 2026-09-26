; $ff07 bit 5 (freeze) stops the horizontal and vertical counters and the
; timers.  Read the counters and timer 1 when the freeze starts and after a
; loop of about 10000 CPU cycles, which would otherwise pass 90 lines, then
; after the freeze.  A raster compare two lines ahead is reached only after
; the freeze.
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
lda #220
jsr waitline
lda #222
sta $ff0b
lda #$ff
sta $ff09
sta $ff00
sta $ff01
lda $ff1e
sta $1800
lda $ff1d
sta $1801
lda $ff07
ora #$20
sta $ff07
lda $ff1e
sta $1802
lda $ff1d
sta $1803
lda $ff00
sta $1804
lda $ff01
sta $1805
ldy #8
loop1:
ldx #0
loop2:
dex
bne loop2
dey
bne loop1
lda $ff1e
sta $1806
lda $ff1d
sta $1807
lda $ff00
sta $1808
lda $ff01
sta $1809
lda $ff09
sta $180a
lda $ff07
and #$df
sta $ff07
lda $ff1e
sta $180b
lda $ff1d
sta $180c
lda $ff00
sta $180d
lda $ff01
sta $180e
ldx #100
loop3:
dex
bne loop3
lda $ff09
sta $180f
lda $ff1d
sta $1810
done:
jmp done

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
