; REQUIRES: native && system-linux && target=ia64{{.*}}
; RUN: llc -filetype=obj -o %t.o %s
; RUN: llvm-jitlink %t.o
;
; An exception that is thrown in one JIT'd function passes through a second
; and is caught in a third. The unwinder finds the unwind table of the graph
; only if the table was registered with it, and without that the program is
; ended by std::terminate.

@_ZTIi = external constant ptr

declare ptr @__cxa_allocate_exception(i64)
declare void @__cxa_throw(ptr, ptr, ptr)
declare ptr @__cxa_begin_catch(ptr)
declare void @__cxa_end_catch()
declare i32 @__gxx_personality_v0(...)
declare i32 @llvm.eh.typeid.for(ptr)

@cleanups = global i32 0

define void @thrower(i32 %v) noinline {
entry:
  %e = call ptr @__cxa_allocate_exception(i64 4)
  store i32 %v, ptr %e
  call void @__cxa_throw(ptr %e, ptr @_ZTIi, ptr null)
  unreachable
}

define void @middle(i32 %v) noinline personality ptr @__gxx_personality_v0 {
entry:
  invoke void @thrower(i32 %v)
          to label %done unwind label %lpad

lpad:
  %l = landingpad { ptr, i32 }
          cleanup
  %c = load i32, ptr @cleanups
  %c1 = add i32 %c, 1
  store i32 %c1, ptr @cleanups
  resume { ptr, i32 } %l

done:
  ret void
}

define i32 @main(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  invoke void @middle(i32 42)
          to label %missed unwind label %lpad

lpad:
  %l = landingpad { ptr, i32 }
          catch ptr @_ZTIi
  %obj = extractvalue { ptr, i32 } %l, 0
  %sel = extractvalue { ptr, i32 } %l, 1
  %id = call i32 @llvm.eh.typeid.for(ptr @_ZTIi)
  %match = icmp eq i32 %sel, %id
  br i1 %match, label %caught, label %missed

caught:
  %p = call ptr @__cxa_begin_catch(ptr %obj)
  %v = load i32, ptr %p
  call void @__cxa_end_catch()
  %c = load i32, ptr @cleanups
  %vok = icmp eq i32 %v, 42
  %cok = icmp eq i32 %c, 1
  %ok = and i1 %vok, %cok
  %r = select i1 %ok, i32 0, i32 1
  ret i32 %r

missed:
  ret i32 2
}
