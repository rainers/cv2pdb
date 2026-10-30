set -ex
cp "$TESTDIR/dwarf5-program.c" block.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -o block.exe block.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -ffunction-sections -o sections.exe block.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -c -o block.o block.c
llvm-objdump -h block.o >block-o.sections
grep ' \.debug_rnglists ' block-o.sections || { echo "test precondition lost: no lexical block with ranges"; exit 1; }
llvm-objdump -h block.exe >block.sections
llvm-nm block.exe >block.nm

# Both conversions read DWARF 5 line tables that have MD5 file checksums.
"$CV2PDB" block.exe block-cv.exe 2>&1 | tee block-cv.log
"$CV2PDB" sections.exe sections-cv.exe 2>&1 | tee sections-cv.log
"$CVDUMP" -s -l block-cv.pdb >block-cv.dump

# Neither conversion logs an error.
test -f block-cv.log
test -f sections-cv.log
if grep ERROR: block-cv.log sections-cv.log; then exit 1; fi

# The procedure records of get and main have their symbol addresses.
text=$(awk '$2 == ".text" { print $4 }' block.sections)
for f in get main; do
  addr=$(awk -v n=$f '$2 == "T" && $3 == n { print $1 }' block.nm)
  off=$(printf '%08X' $((0x$addr - 0x$text)))
  grep "S_GPROC32: \[0001:$off\], .*, $f\$" block-cv.dump
done

# Every lexical block of main starts inside main, and main has at least one.
awk '$2 == "S_GPROC32:" { split($3, a, /[:\]]/); lo = strtonum("0x" a[2]); hi = lo + strtonum("0x" substr($5, 1, 8)); inmain = $NF == "main"; }
     $2 == "S_BLOCK32:" && inmain { split($3, a, /[:\]]/); o = strtonum("0x" a[2]); n++; if (o < lo || o >= hi) { print; bad++; } }
     END {exit !(n && !bad); }' block-cv.dump
