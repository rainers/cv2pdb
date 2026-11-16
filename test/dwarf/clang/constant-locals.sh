set -ex
cp "$TESTDIR/constant-locals.c" count.c

clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -c -o count.o count.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o count.exe count.o
llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=count count.o >count.objdump
expand count.objdump >count.vars
awk -v var=i -f "$TESTDIR/objdump-var-ranges.awk" count.vars >count.ranges
cat count.ranges
awk '$3 == "lit" { n++; lit = $2 } $3 == "loc" && n == 1 && $1 >= lit { reg = 1 } END { exit !(n == 1 && reg) }' count.ranges ||
  { echo "test precondition lost: the DWARF of count() does not give 'i' a single DW_OP_lit<n> range followed by a register range"; exit 1; }
"$CV2PDB" count.exe count-cv.exe
"$CVDUMP" -s count-cv.pdb >count-cv.dump

# count() has one S_CONSTANT record for `i`, with the value of the DW_OP_lit<n> range of `i`, in
# an S_BLOCK32 record that spans exactly that range.
awk 'FNR == NR { if ($3 == "lit") { lo = $1; hi = $2; val = $4 } next }
     $2 == "S_GPROC32:" { inp = $NF == "count"; d = 0; if (!inp) next; split($3, a, /[:\]]/); base = strtonum("0x" a[2]); size = strtonum("0x" substr($5, 1, 8)); next }
     !inp { next }
     $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; blo[d] = strtonum("0x" a[2]) - base; bhi[d] = blo[d] + strtonum("0x" substr($5, 1, 8)); next }
     $2 == "S_END" { if (d) d--; else inp = 0; next }
     $2 == "S_CONSTANT:" && $NF == "i" {
       print
       n++
       if (hi == "end") hi = size
       if (!d || blo[d] != lo || bhi[d] != hi || $(NF - 1) != val ",") bad = 1
     }
     END { exit bad || n != 1 }' count.ranges count-cv.dump
