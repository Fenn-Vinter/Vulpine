; ModuleID = 'VulpineModule'
source_filename = "VulpineModule"

; Function Attrs: norecurse
define i32 @main() #0 {
entry:
  %x = alloca i32, align 4
  store i32 5, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 3, ptr %y, align 4
  %z = alloca i32, align 4
  %x1 = load i32, ptr %x, align 4
  %y2 = load i32, ptr %y, align 4
  %addtmp = add i32 %x1, %y2
  store i32 %addtmp, ptr %z, align 4
  %z3 = load i32, ptr %z, align 4
  %addtmp4 = add i32 %z3, 10
  ret i32 %addtmp4
}

attributes #0 = { norecurse }
