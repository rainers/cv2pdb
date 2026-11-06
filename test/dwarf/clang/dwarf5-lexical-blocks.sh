set -ex
cp "$TESTDIR/dwarf5-program.c" block.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -o block.exe block.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -c -o block.o block.c
llvm-objdump -h block.o >block-o.sections
grep ' \.debug_rnglists ' block-o.sections || { echo "test precondition lost: no lexical block with ranges"; exit 1; }
"$CV2PDB" block.exe block-cv.exe
"$CVDUMP" -s block-cv.pdb >block-cv.dump

# Every lexical block of main starts inside main, and main has at least one.
awk '$2 == "S_GPROC32:" { split($3, a, /[:\]]/); lo = strtonum("0x" a[2]); hi = lo + strtonum("0x" substr($5, 1, 8)); inmain = $NF == "main"; }
     $2 == "S_BLOCK32:" && inmain { split($3, a, /[:\]]/); o = strtonum("0x" a[2]); n++; if (o < lo || o >= hi) { print; bad++; } }
     END {exit !(n && !bad); }' block-cv.dump
