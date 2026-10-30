set -ex
cp "$TESTDIR/procedures-at-4gb-boundary.c" edge.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -O1 -o edge-probe.exe edge.c
base=$(llvm-objdump -p edge-probe.exe | awk '$1 == "ImageBase" { print $2 }')
main=$(llvm-nm edge-probe.exe | awk '$3 == "main" { print $1 }')
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -O1 -Wl,--image-base=$(printf '0x%x' $(((1 << 32) - (0x$main - 0x$base)))) -o edge.exe edge.c
llvm-objdump -h edge.exe >edge.sections
llvm-nm edge.exe >edge.nm
awk '$3 == "main" { found = strtonum("0x" $1) == 2^32 } END { exit !found }' edge.nm || { echo "test precondition lost: main does not start at 4 GB"; exit 1; }
"$CV2PDB" edge.exe edge-cv.exe
"$CVDUMP" -s edge-cv.pdb >edge-cv.dump
text=$(awk '$2 == ".text" { print $4 }' edge.sections)
addr=$(awk '$2 == "T" && $3 == "main" { print $1 }' edge.nm)
off=$(printf '%08X' $((0x$addr - 0x$text)))
grep "S_GPROC32: \[0001:$off\], .*, main\$" edge-cv.dump
