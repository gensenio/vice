; Hires bitmap: the pixels uncovered by a horizontal scroll take the colour
; of the 0 pixels of the last character, (attribute & $70) | (video & $0f).
; Every cell has attribute $70 and video $01 (white 0 pixels); the old
; overscan colour, video & $7f = $01, is dark grey.  Line 100 starts with
; a scroll of 4 and is set back to 0 in the middle of the line, so it is
; drawn with raster changes.  Vertical scroll 0 keeps line 100 free of DMA.
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
lda #$20
sta fill+2
lda #0
tax
fill:
sta $2000,x
inx
bne fill
inc fill+2
ldy fill+2
cpy #$40
bne fill
colours:
lda #$70
!for p,0,3 { sta $0800+p*256,x }
lda #$01
!for p,0,3 { sta $0c00+p*256,x }
inx
bne colours
lda $ff12
and #$c3
ora #$08
sta $ff12
lda #$38
sta $ff06

frame:
lda #250
jsr waitline
lda #$0c
sta $ff07
lda #99
jsr waitline
lda #100
jsr waitmid
lda #$08
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
; early in its display window.
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
