; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; A switch needs 64 cases for a jump table; fewer are compared and branched
; on.
; CHECK-LABEL: sixty_three:
; CHECK-NOT:   mov b7
; CHECK:       .endp sixty_three
define i64 @sixty_three(i64 %a) {
  switch i64 %a, label %d [ i64 0, label %b0
                            i64 1, label %b1
                            i64 2, label %b2
                            i64 3, label %b3
                            i64 4, label %b4
                            i64 5, label %b5
                            i64 6, label %b6
                            i64 7, label %b7
                            i64 8, label %b8
                            i64 9, label %b9
                            i64 10, label %b10
                            i64 11, label %b11
                            i64 12, label %b12
                            i64 13, label %b13
                            i64 14, label %b14
                            i64 15, label %b15
                            i64 16, label %b16
                            i64 17, label %b17
                            i64 18, label %b18
                            i64 19, label %b19
                            i64 20, label %b20
                            i64 21, label %b21
                            i64 22, label %b22
                            i64 23, label %b23
                            i64 24, label %b24
                            i64 25, label %b25
                            i64 26, label %b26
                            i64 27, label %b27
                            i64 28, label %b28
                            i64 29, label %b29
                            i64 30, label %b30
                            i64 31, label %b31
                            i64 32, label %b32
                            i64 33, label %b33
                            i64 34, label %b34
                            i64 35, label %b35
                            i64 36, label %b36
                            i64 37, label %b37
                            i64 38, label %b38
                            i64 39, label %b39
                            i64 40, label %b40
                            i64 41, label %b41
                            i64 42, label %b42
                            i64 43, label %b43
                            i64 44, label %b44
                            i64 45, label %b45
                            i64 46, label %b46
                            i64 47, label %b47
                            i64 48, label %b48
                            i64 49, label %b49
                            i64 50, label %b50
                            i64 51, label %b51
                            i64 52, label %b52
                            i64 53, label %b53
                            i64 54, label %b54
                            i64 55, label %b55
                            i64 56, label %b56
                            i64 57, label %b57
                            i64 58, label %b58
                            i64 59, label %b59
                            i64 60, label %b60
                            i64 61, label %b61
                            i64 62, label %b62 ]
b0:
  ret i64 3
b1:
  ret i64 10
b2:
  ret i64 17
b3:
  ret i64 24
b4:
  ret i64 31
b5:
  ret i64 38
b6:
  ret i64 45
b7:
  ret i64 52
b8:
  ret i64 59
b9:
  ret i64 66
b10:
  ret i64 73
b11:
  ret i64 80
b12:
  ret i64 87
b13:
  ret i64 94
b14:
  ret i64 101
b15:
  ret i64 108
b16:
  ret i64 115
b17:
  ret i64 122
b18:
  ret i64 129
b19:
  ret i64 136
b20:
  ret i64 143
b21:
  ret i64 150
b22:
  ret i64 157
b23:
  ret i64 164
b24:
  ret i64 171
b25:
  ret i64 178
b26:
  ret i64 185
b27:
  ret i64 192
b28:
  ret i64 199
b29:
  ret i64 206
b30:
  ret i64 213
b31:
  ret i64 220
b32:
  ret i64 227
b33:
  ret i64 234
b34:
  ret i64 241
b35:
  ret i64 248
b36:
  ret i64 255
b37:
  ret i64 262
b38:
  ret i64 269
b39:
  ret i64 276
b40:
  ret i64 283
b41:
  ret i64 290
b42:
  ret i64 297
b43:
  ret i64 304
b44:
  ret i64 311
b45:
  ret i64 318
b46:
  ret i64 325
b47:
  ret i64 332
b48:
  ret i64 339
b49:
  ret i64 346
b50:
  ret i64 353
b51:
  ret i64 360
b52:
  ret i64 367
b53:
  ret i64 374
b54:
  ret i64 381
b55:
  ret i64 388
b56:
  ret i64 395
b57:
  ret i64 402
b58:
  ret i64 409
b59:
  ret i64 416
b60:
  ret i64 423
b61:
  ret i64 430
b62:
  ret i64 437
d:
  ret i64 0
}

; CHECK-LABEL: sixty_four:
; CHECK:       @ltoffx(.LJTI1_0)
; CHECK:       br.cond.sptk.few b7
; CHECK:       .endp sixty_four
define i64 @sixty_four(i64 %a) {
  switch i64 %a, label %d [ i64 0, label %b0
                            i64 1, label %b1
                            i64 2, label %b2
                            i64 3, label %b3
                            i64 4, label %b4
                            i64 5, label %b5
                            i64 6, label %b6
                            i64 7, label %b7
                            i64 8, label %b8
                            i64 9, label %b9
                            i64 10, label %b10
                            i64 11, label %b11
                            i64 12, label %b12
                            i64 13, label %b13
                            i64 14, label %b14
                            i64 15, label %b15
                            i64 16, label %b16
                            i64 17, label %b17
                            i64 18, label %b18
                            i64 19, label %b19
                            i64 20, label %b20
                            i64 21, label %b21
                            i64 22, label %b22
                            i64 23, label %b23
                            i64 24, label %b24
                            i64 25, label %b25
                            i64 26, label %b26
                            i64 27, label %b27
                            i64 28, label %b28
                            i64 29, label %b29
                            i64 30, label %b30
                            i64 31, label %b31
                            i64 32, label %b32
                            i64 33, label %b33
                            i64 34, label %b34
                            i64 35, label %b35
                            i64 36, label %b36
                            i64 37, label %b37
                            i64 38, label %b38
                            i64 39, label %b39
                            i64 40, label %b40
                            i64 41, label %b41
                            i64 42, label %b42
                            i64 43, label %b43
                            i64 44, label %b44
                            i64 45, label %b45
                            i64 46, label %b46
                            i64 47, label %b47
                            i64 48, label %b48
                            i64 49, label %b49
                            i64 50, label %b50
                            i64 51, label %b51
                            i64 52, label %b52
                            i64 53, label %b53
                            i64 54, label %b54
                            i64 55, label %b55
                            i64 56, label %b56
                            i64 57, label %b57
                            i64 58, label %b58
                            i64 59, label %b59
                            i64 60, label %b60
                            i64 61, label %b61
                            i64 62, label %b62
                            i64 63, label %b63 ]
b0:
  ret i64 3
b1:
  ret i64 10
b2:
  ret i64 17
b3:
  ret i64 24
b4:
  ret i64 31
b5:
  ret i64 38
b6:
  ret i64 45
b7:
  ret i64 52
b8:
  ret i64 59
b9:
  ret i64 66
b10:
  ret i64 73
b11:
  ret i64 80
b12:
  ret i64 87
b13:
  ret i64 94
b14:
  ret i64 101
b15:
  ret i64 108
b16:
  ret i64 115
b17:
  ret i64 122
b18:
  ret i64 129
b19:
  ret i64 136
b20:
  ret i64 143
b21:
  ret i64 150
b22:
  ret i64 157
b23:
  ret i64 164
b24:
  ret i64 171
b25:
  ret i64 178
b26:
  ret i64 185
b27:
  ret i64 192
b28:
  ret i64 199
b29:
  ret i64 206
b30:
  ret i64 213
b31:
  ret i64 220
b32:
  ret i64 227
b33:
  ret i64 234
b34:
  ret i64 241
b35:
  ret i64 248
b36:
  ret i64 255
b37:
  ret i64 262
b38:
  ret i64 269
b39:
  ret i64 276
b40:
  ret i64 283
b41:
  ret i64 290
b42:
  ret i64 297
b43:
  ret i64 304
b44:
  ret i64 311
b45:
  ret i64 318
b46:
  ret i64 325
b47:
  ret i64 332
b48:
  ret i64 339
b49:
  ret i64 346
b50:
  ret i64 353
b51:
  ret i64 360
b52:
  ret i64 367
b53:
  ret i64 374
b54:
  ret i64 381
b55:
  ret i64 388
b56:
  ret i64 395
b57:
  ret i64 402
b58:
  ret i64 409
b59:
  ret i64 416
b60:
  ret i64 423
b61:
  ret i64 430
b62:
  ret i64 437
b63:
  ret i64 444
d:
  ret i64 0
}
