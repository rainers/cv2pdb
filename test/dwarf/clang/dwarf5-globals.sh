set -ex
cp "$TESTDIR/dwarf5-globals.c" global.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O0 -o global.exe global.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O0 -S -o global.s global.c
awk '/# DW_AT_location$/ { getline; if ($2 == 161) n++ } END { exit !n }' global.s || { echo "test precondition lost: no DW_OP_addrx location"; exit 1; }
llvm-objdump -h global.exe >global.sections
llvm-nm global.exe >global.nm
"$CV2PDB" global.exe global-cv.exe
"$CVDUMP" -g global-cv.pdb >global-cv.dump

seg=$(awk '$2 == ".data" { printf "%04X", $1 + 1 }' global.sections)
data=$(awk '$2 == ".data" { print $4 }' global.sections)
for v in g_counter s_counter; do
  addr=$(awk -v n=$v '$3 == n { print $1 }' global.nm)
  off=$(printf '%08X' $((0x$addr - 0x$data)))
  grep "S_GDATA32: \[$seg:$off\], .*, $v\$" global-cv.dump
done
