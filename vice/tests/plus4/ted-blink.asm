; Read the blink counter ($ff1f bits 3-6) on lines 204 and 206 of one frame
; and on line 204 of the next: it advances on line 205, once per frame.
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
lda #250
jsr waitline
lda #204
jsr waitline
lda $ff1f
sta $1800
lda #206
jsr waitline
lda $ff1f
sta $1801
lda #250
jsr waitline
lda #204
jsr waitline
lda $ff1f
sta $1802
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
