set -ex
cp "$TESTDIR/dwarf4-globals-above-4gb.c" global.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-4 -O0 -o global.exe global.c
llvm-objdump -p global.exe >global.headers
awk '$1 == "ImageBase" { found = strtonum("0x" $2) >= 2^32 } END { exit !found }' global.headers || { echo "test precondition lost: image base below 4 GB"; exit 1; }
llvm-objdump -h global.exe >global.sections
llvm-nm global.exe >global.nm
"$CV2PDB" global.exe global-cv.exe
"$CVDUMP" -g global-cv.pdb >global-cv.globals

for v in g_counter s_total; do
  addr=$(awk -v n=$v '$3 == n { print $1 }' global.nm)
  test -n "$addr"
  loc=$(awk -v a=$((0x$addr)) '$1 ~ /^[0-9]+$/ { lo = strtonum("0x" $4); if (lo <= a && a < lo + strtonum("0x" $3)) printf "[%04X:%08X],", $1 + 1, a - lo }' global.sections)
  awk -v a="$loc" -v n=", $v" '($1 == "S_GDATA32:" || $1 == "S_LDATA32:") && $2 == a && substr($0, length($0) - length(n) + 1) == n { found=1 } END {exit !found; }' global-cv.globals
done
