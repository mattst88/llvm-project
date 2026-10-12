; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

; A virtual table holds the descriptors of the virtual functions, and not
; pointers to them, so that a pointer into the table is a pointer to a
; function. A table says so with an attribute, and then a pointer to a
; function and the null pointer after it are the two words that the linker
; fills in with the descriptor.

declare void @extern()

define void @local() {
  ret void
}

define internal void @internal() {
  ret void
}

@alias = alias void (), ptr @local

@object = global i64 0

; CHECK-LABEL: table:
; CHECK-NEXT:    data8.ua 16
; CHECK-NEXT:    data8.ua object
; CHECK-NEXT:    data16.ua @iplt(extern)
; CHECK-NEXT:    data16.ua @iplt(local)
; CHECK-NEXT:    data16.ua @iplt(internal)
; CHECK-NEXT:    data16.ua @iplt(alias)
; CHECK-NEXT:    data8.ua 0
; CHECK-NEXT:    data8.ua 0
; CHECK-NEXT:    data8.ua -16
; CHECK-NEXT:    data8.ua object
; CHECK-NEXT:    data16.ua @iplt(local)
; CHECK-NEXT:    .size table, 128
@table = global { [12 x ptr], [4 x ptr] } {
  [12 x ptr] [ptr inttoptr (i64 16 to ptr), ptr @object,
              ptr @extern, ptr null, ptr @local, ptr null,
              ptr @internal, ptr null, ptr @alias, ptr null,
              ptr null, ptr null],
  [4 x ptr] [ptr inttoptr (i64 -16 to ptr), ptr @object,
             ptr @local, ptr null]
}, align 8 #0

; A table of one part is an array.

; CHECK-LABEL: array:
; CHECK-NEXT:    data8.ua object
; CHECK-NEXT:    data16.ua @iplt(extern)
; CHECK-NEXT:    .size array, 24
@array = global [3 x ptr] [ptr @object, ptr @extern, ptr null], align 8 #0

; A pointer to a function that no null pointer follows is a pointer.

; CHECK-LABEL: pointers:
; CHECK-NEXT:    data8.ua @fptr(extern)
; CHECK-NEXT:    data8.ua @fptr(local)
; CHECK-NEXT:    data16.ua @iplt(extern)
; CHECK-NEXT:    data8.ua @fptr(local)
@pointers = global [5 x ptr] [ptr @extern, ptr @local, ptr @extern, ptr null,
                              ptr @local], align 8 #0

; So is every one in a variable that does not have the attribute.

; CHECK-LABEL: plain:
; CHECK-NEXT:    data8.ua @fptr(extern)
; CHECK-NEXT:    data8.ua 0
@plain = global [2 x ptr] [ptr @extern, ptr null], align 8

; A sanitizer wraps the table in a structure with a red zone after it, and
; the table still holds descriptors. What the layout leaves between the
; members is still there.

; CHECK-LABEL: wrapped:
; CHECK-NEXT:    data4.ua 7
; CHECK-NEXT:    .zero 4
; CHECK-NEXT:    data8.ua 0
; CHECK-NEXT:    data16.ua @iplt(extern)
; CHECK-NEXT:    .zero 8
; CHECK-NEXT:    .size wrapped, 40
@wrapped = global { i32, { [3 x ptr] }, [8 x i8] }
                  { i32 7,
                    { [3 x ptr] } { [3 x ptr] [ptr null, ptr @extern, ptr null] },
                    [8 x i8] zeroinitializer }, align 8 #0

; RELOC:      .rela.data {
; RELOC-NEXT:   0x8 R_IA64_DIR64LSB object 0x0
; RELOC-NEXT:   0x10 R_IA64_IPLTLSB extern 0x0
; RELOC-NEXT:   0x20 R_IA64_IPLTLSB local 0x0
; RELOC-NEXT:   0x30 R_IA64_IPLTLSB .text 0x10
; RELOC-NEXT:   0x40 R_IA64_IPLTLSB alias 0x0
; RELOC-NEXT:   0x68 R_IA64_DIR64LSB object 0x0
; RELOC-NEXT:   0x70 R_IA64_IPLTLSB local 0x0
; RELOC-NEXT:   0x80 R_IA64_DIR64LSB object 0x0
; RELOC-NEXT:   0x88 R_IA64_IPLTLSB extern 0x0
; RELOC-NEXT:   0x98 R_IA64_FPTR64LSB extern 0x0
; RELOC-NEXT:   0xA0 R_IA64_FPTR64LSB local 0x0
; RELOC-NEXT:   0xA8 R_IA64_IPLTLSB extern 0x0
; RELOC-NEXT:   0xB8 R_IA64_FPTR64LSB local 0x0
; RELOC-NEXT:   0xC0 R_IA64_FPTR64LSB extern 0x0
; RELOC-NEXT:   0xE0 R_IA64_IPLTLSB extern 0x0
; RELOC-NEXT: }

attributes #0 = { "function-descriptors" }
