set -ex
cp "$TESTDIR/dwarf5-program.c" block.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -o block.exe block.c
llvm-objdump -h block.exe >block.sections
llvm-nm block.exe >block.nm
"$CV2PDB" block.exe block-cv.exe
"$CVDUMP" -s block-cv.pdb >block-cv.dump

# The procedure records of get and main have their symbol addresses.
text=$(awk '$2 == ".text" { print $4 }' block.sections)
for f in get main; do
  addr=$(awk -v n=$f '$2 == "T" && $3 == n { print $1 }' block.nm)
  off=$(printf '%08X' $((0x$addr - 0x$text)))
  grep "S_GPROC32: \[0001:$off\], .*, $f\$" block-cv.dump
done
