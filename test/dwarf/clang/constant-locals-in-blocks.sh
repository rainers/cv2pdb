set -ex
cp "$TESTDIR/constant-locals-in-blocks.c" shadow.c

clang --target=x86_64-w64-mingw32 -g -gdwarf-4 -O2 -c -o shadow.o shadow.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o shadow.exe shadow.o
llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=shadow shadow.o >shadow.objdump
expand shadow.objdump >shadow.vars
awk -v var=x -f "$TESTDIR/objdump-var-ranges.awk" shadow.vars >shadow.ranges
cat shadow.ranges
# The outer `x` is only ever a constant and the inner `x` is only ever in a register, so a constant
# range that overlaps a register range is a constant of the outer `x` inside the block of the inner
# `x`.
awk '$3 == "lit" { clo[++nc] = $1; chi[nc] = $2 }
     $3 == "loc" { rlo[++nr] = $1; rhi[nr] = $2 }
     END { for (i = 1; i <= nc; i++) for (j = 1; j <= nr; j++) if (clo[i] < rhi[j] && rlo[j] < chi[i]) found = 1; exit !found }' shadow.ranges ||
  { echo "test precondition lost: no DW_OP_lit<n> range of 'x' in shadow() overlaps a register range of 'x'"; exit 1; }
"$CV2PDB" shadow.exe shadow-cv.exe
"$CVDUMP" -s shadow-cv.pdb >shadow-cv.dump

# An address inside an S_BLOCK32 record that has an S_LOCAL record for `x` is covered by no
# S_CONSTANT record for `x`. Every other address in a constant range of `x` in shadow() is covered
# by exactly one S_CONSTANT record for `x`, with the value of that range. No S_CONSTANT record for
# `x` covers an address outside the constant ranges of `x`.
awk 'FNR == NR { if ($3 == "lit") for (o = $1; o < ($2 == "end" ? 65536 : $2); o++) want[o] = $4; next }
     $2 == "S_GPROC32:" { inp = $NF == "shadow"; d = 0; if (!inp) next; split($3, a, /[:\]]/); base = strtonum("0x" a[2]); size = strtonum("0x" substr($5, 1, 8)); next }
     !inp { next }
     $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; blo[d] = strtonum("0x" a[2]) - base; bhi[d] = blo[d] + strtonum("0x" substr($5, 1, 8)); next }
     $2 == "S_END" { if (d) d--; else inp = 0; next }
     $2 == "S_LOCAL:" && $NF == "x" && d { for (o = blo[d]; o < bhi[d]; o++) hidden[o] = 1; next }
     $2 == "S_CONSTANT:" && $NF == "x" {
       print
       v = $(NF - 1); sub(/,$/, "", v)
       for (o = d ? blo[d] : 0; o < (d ? bhi[d] : size); o++) { got[o]++; val[o] = v }
     }
     END {
       for (o = 0; o < size; o++) {
         if ((o in hidden) || !(o in want)) { if (got[o]) { print "offset " o ": " got[o] " S_CONSTANT records for `x`, want 0 because " ((o in hidden) ? "an S_LOCAL record for `x` covers it" : "no constant range of `x` covers it"); bad = 1 } }
         else if (got[o] != 1 || val[o] != want[o]) { print "offset " o ": " got[o] + 0 " S_CONSTANT records for `x`, value " val[o] ", want " want[o]; bad = 1 }
       }
       exit bad
     }' shadow.ranges shadow-cv.dump
