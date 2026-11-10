set -ex
cp "$TESTDIR/cfa-relative-locals.c" frame.c

for v in 4 5; do
  gcc -g -gdwarf-$v -O2 -dA -S -o frame-d$v.s frame.c
  grep -q DW_OP_call_frame_cfa frame-d$v.s || { echo "test precondition lost: frame base is not the CFA"; exit 1; }
  awk '/^framed:/ { f = 1 }
       f && /\.cfi_def_cfa_offset/ { last = $2; if ($2 > max) max = $2 }
       f && /\.cfi_(restore|remember_state)/ { restore = 1 }
       f && /\.cfi_endproc/ { ok = last == 8 && max > 8 && !restore; exit }
       END { exit !ok }' frame-d$v.s ||
    { echo "test precondition lost: the frame description of framed() has a remember or restore instruction, allocates no stack frame, or does not end with the frame released"; exit 1; }
  gcc -g -gdwarf-$v -O2 -o frame-d$v.exe frame.c
  objdump -h frame-d$v.exe | grep ' \.debug_frame ' || { echo "test precondition lost: frame-d$v.exe has no .debug_frame section"; exit 1; }
  objdump -d --no-show-raw-insn --disassemble=framed frame-d$v.exe >frame-d$v.asm
  "$CV2PDB" frame-d$v.exe frame-cv-d$v.exe
  "$CVDUMP" -s frame-cv-d$v.pdb >frame-cv-d$v.dump
  awk 'FNR == NR { if (match($0, /0x[0-9a-f]+\(%rsp\),%rcx$/)) { slot = strtonum(substr($0, RSTART, RLENGTH - 11)); n++ } next }
       $2 == "S_GPROC32:" { inp = $NF == "framed"; d = 0 }
       inp && $2 == "S_BLOCK32:" { d++ }
       $2 == "S_END" { if (d) d--; else inp = 0 }
       inp && $2 == "S_LOCAL:" { var = $NF; next }
       inp && var == "local" && $2 == "S_DEFRANGE_REGISTER_REL:" { print; if ($3 == "[rsp" && strtonum("0x" $5) == slot) ok = 1 }
       $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
       END { if (n != 1) { printf "test precondition lost: framed() has %d instructions moving an rsp-relative operand into rcx, not 1\n", n; exit 1 } printf "expected local at rsp+%x\n", slot; exit !ok }' frame-d$v.asm frame-cv-d$v.dump
done
