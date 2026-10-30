set -ex
cp "$TESTDIR/large-procedure-locals.c" big.c

clang --target=x86_64-w64-mingw32 -g -O2 -c -o big.o big.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o big.exe big.o
llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=big big.o >big.vars
awk '/^ *[0-9a-f]+:/ { last = strtonum("0x" substr($1, 1, length($1) - 1)) }
     /[\/|]- seed = R(BX|SI|DI|BP|1[2-5]) *$/ { saved = 1 }
     END { exit !(last > 61440 && saved) }' big.vars ||
  { echo "test precondition lost: big() is not longer than 0xF000 bytes, or the DWARF never locates parameter 'seed' in a callee-saved register"; exit 1; }
"$CV2PDB" big.exe big-cv.exe
"$CVDUMP" -s big-cv.pdb >big-cv.dump

# Ranges of the variables of big() stay inside it and span at most 0xF000 bytes, so the location of
# `seed` in one register is split into consecutive records.
awk '$2 == "S_GPROC32:" { inp = $NF == "big"; split($3, a, /[:\]]/); lo = strtonum("0x" a[2]); hi = lo + strtonum("0x" substr($5, 1, 8)); var = ""; next }
     !inp { next }
     $2 == "S_LOCAL:" { var = $NF; prev = ""; next }
     $2 ~ /^S_DEFRANGE_/ { reg = $2 == "S_DEFRANGE_REGISTER:" ? $3 : ""; next }
     $1 == "Range:" {
       split($2, s, /[:\]]/); split($4, e, /[:\]]/); rs = strtonum("0x" s[2]); re = strtonum("0x" e[2])
       if (rs < lo || re > hi || re <= rs || re - rs > 61440) { print; bad = 1 }
       if (var == "seed" && reg != "" && reg == prev && rs == end) { print; chained = 1 }
       prev = reg; end = re
     }
     $2 ~ /^S_/ { var = "" }
     END { exit !(chained && !bad) }' big-cv.dump
