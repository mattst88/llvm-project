; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -r -x .IA_64.unwind_info %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ
; RUN: llvm-objdump -t %t.o | FileCheck %s --check-prefix=SYM

; The exception table of a function is the handler data of its unwind
; information: it follows the personality routine there, and the procedure
; ends after it. The personality routine hands a landing pad the exception in
; r15 and the selector in r16, and not the gp of the function, which the
; landing pad restores.

@_ZTIi = external constant ptr

declare void @may_throw()
declare i32 @__gxx_personality_v0(...)
declare ptr @__cxa_begin_catch(ptr)
declare void @__cxa_end_catch()
declare i32 @llvm.eh.typeid.for.p0(ptr)

define i32 @catch_int() personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: catch_int:
; CHECK:       .Lfunc_begin0:
; CHECK:         mov r33 = r1
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0 ;;
; CHECK-NEXT:    .body
; CHECK-NEXT:  .Ltmp0:
; CHECK-NEXT:    br.call.sptk.many b0 = may_throw ;;
; CHECK-NEXT:    mov r1 = r33 ;;
; CHECK-NEXT:  .Ltmp1:
; CHECK:       .Ltmp2:
; CHECK-NEXT:    mov r1 = r33
; CHECK-NEXT:    cmp4.eq p7, p6 = 1, r16
; CHECK-NEXT:    (p6) br.cond.dpnt.few .LBB0_5 ;;
; CHECK-NEXT:  // %bb.4: // %catch
; CHECK-NEXT:    mov r36 = r15
; CHECK-NEXT:    br.call.sptk.many b0 = __cxa_begin_catch ;;
; CHECK:       .LBB0_5: // %resume
; CHECK-NEXT:    mov r36 = r15
; CHECK-NEXT:    br.call.sptk.many b0 = _Unwind_Resume ;;
; CHECK-NEXT:    mov r1 = r33 ;;
; CHECK-NEXT:    .p2align 4, 0x0
; CHECK-NEXT:  .Lfunc_end0:
; CHECK-NEXT:    .size catch_int, .Lfunc_end0-catch_int
; CHECK-NEXT:    .personality __gxx_personality_v0
; CHECK-NEXT:    .handlerdata
; CHECK-NEXT:    .p2align 2, 0x0
; CHECK-NEXT:  GCC_except_table0:
; CHECK-NEXT:  .Lexception0:
; CHECK-NEXT:    data1 255 // @LPStart Encoding = omit
; CHECK-NEXT:    data1 180 // @TType Encoding
; CHECK-NEXT:    .uleb128 .Lttbase0-.Lttbaseref0
; CHECK-NEXT:  .Lttbaseref0:
; CHECK-NEXT:    data1 1 // Call site Encoding = uleb128
; CHECK-NEXT:    .uleb128 .Lcst_end0-.Lcst_begin0
; CHECK-NEXT:  .Lcst_begin0:
; CHECK-NEXT:    .uleb128 .Ltmp0-.Lfunc_begin0 // >> Call Site 1 <<
; CHECK-NEXT:    .uleb128 .Ltmp1-.Ltmp0 // Call between .Ltmp0 and .Ltmp1
; CHECK-NEXT:    .uleb128 .Ltmp2-.Lfunc_begin0 // jumps to .Ltmp2
; CHECK-NEXT:    data1 3 // On action: 2
; CHECK-NEXT:    .uleb128 .Ltmp1-.Lfunc_begin0 // >> Call Site 2 <<
; CHECK-NEXT:    .uleb128 .Lfunc_end0-.Ltmp1 // Call between .Ltmp1 and .Lfunc_end0
; CHECK-NEXT:    data1 0 // has no landing pad
; CHECK-NEXT:    data1 0 // On action: cleanup
; CHECK-NEXT:  .Lcst_end0:
; CHECK-NEXT:    data1 0 // >> Action Record 1 <<
; CHECK-NEXT:    // Cleanup
; CHECK-NEXT:    data1 0 // No further actions
; CHECK-NEXT:    data1 1 // >> Action Record 2 <<
; CHECK-NEXT:    // Catch TypeInfo 1
; CHECK-NEXT:    data1 125 // Continue to action 1
; CHECK-NEXT:    .p2align 2, 0x0
; CHECK-NEXT:    // >> Catch TypeInfos <<
; CHECK-NEXT:    data8.ua @gprel(.L_ZTIi.DW.stub) // TypeInfo 1
; CHECK-NEXT:  .Lttbase0:
; CHECK-NEXT:    .p2align 2, 0x0
; CHECK-NEXT:    .text
; CHECK-NEXT:    .endp catch_int
entry:
  invoke void @may_throw()
          to label %cont unwind label %lpad

cont:
  ret i32 0

lpad:
  %lp = landingpad { ptr, i32 }
          cleanup
          catch ptr @_ZTIi
  %exn = extractvalue { ptr, i32 } %lp, 0
  %sel = extractvalue { ptr, i32 } %lp, 1
  %tid = call i32 @llvm.eh.typeid.for.p0(ptr @_ZTIi)
  %match = icmp eq i32 %sel, %tid
  br i1 %match, label %catch, label %resume

catch:
  %p = call ptr @__cxa_begin_catch(ptr %exn)
  %v = load i32, ptr %p
  call void @__cxa_end_catch()
  ret i32 %v

resume:
  resume { ptr, i32 } %lp
}

; A function that has a personality routine and no landing pad has no
; exception table, and nothing for the routine to find.

define void @no_pad() personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: no_pad:
; CHECK-NOT:     .personality
; CHECK:         br.ret.sptk.many b0 ;;
; CHECK-NEXT:    .endp no_pad
  call void @may_throw()
  ret void
}

; The table reaches a type through a pointer to it, which the dynamic linker
; fills in.

; CHECK:         .data
; CHECK-NEXT:    .p2align 3, 0x0
; CHECK-NEXT:  .L_ZTIi.DW.stub:
; CHECK-NEXT:    data8.ua _ZTIi

; The unwind information says that it has a personality routine, for an
; exception and for a forced unwind, and the function ends where its last
; bundle does.

; OBJ:      Section ({{.*}}) .rela.IA_64.unwind_info {
; OBJ-NEXT:   0x18 R_IA64_LTOFF_FPTR64LSB __gxx_personality_v0 0x0
; OBJ-NEXT:   0x34 R_IA64_GPREL64LSB .data 0x0
; OBJ-NEXT: }
; OBJ:      Section ({{.*}}) .rela.data {
; OBJ-NEXT:   0x0 R_IA64_DIR64LSB _ZTIi 0x0
; OBJ-NEXT: }
; OBJ:      0x00000000 02000000 03000100
; OBJ:      0x00000020 ffb41901 09102060 03309001 00000000
; OBJ-NEXT: 0x00000030 017d0000 00000000 00000000 00000000
; OBJ-NEXT: 0x00000040 02000000 00000100

; SYM: 0000000000000000 g F .text 00000000000000c0 catch_int
; SYM: 00000000000000c0 g F .text 0000000000000040 no_pad
