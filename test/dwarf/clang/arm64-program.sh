set -ex
cp "$TESTDIR/arm64-program.c" arm64.c

for o in O0 O2; do
  clang --target=aarch64-w64-mingw32 -fuse-ld=lld -g -$o -nostdlib -mno-stack-arg-probe -Wl,-e,start -o arm64-$o.exe arm64.c
  clang --target=aarch64-w64-mingw32 -g -$o -mno-stack-arg-probe -S -o arm64-$o.s arm64.c
  llvm-objdump -f arm64-$o.exe | grep 'file format coff-arm64' || { echo "test precondition lost: arm64-$o.exe is not an ARM64 image"; exit 1; }
  awk '/DW_AT_frame_base/ { getline; if ($2 == "109") fp++; if ($2 == "111") sp++; } END { exit !(fp && sp); }' arm64-$o.s ||
    { echo "test precondition lost: arm64-$o.s does not have both a frame base in FP (DW_OP_reg29) and one in SP (DW_OP_reg31)"; exit 1; }
done

for o in O0 O2; do
  "$CV2PDB" arm64-$o.exe arm64-$o-cv.exe 2>&1 | tee arm64-$o-cv.log
  "$CVDUMP" -s arm64-$o-cv.pdb >arm64-$o-cv.dump
done

grep '^ *Target processor: ARM64$' arm64-O0-cv.dump
grep '^ *Target processor: ARM64$' arm64-O2-cv.dump

for o in O0 O2; do
  awk '$2 == "S_DEFRANGE_REGISTER_REL:" { r = substr($3, 2); n[r]++; if (r != "sp" && r != "fp" && r !~ /^x([0-9]|1[0-9]|2[0-8])$/) { print; bad++; } }
       END { exit !(n["sp"] && n["fp"] && !bad); }' arm64-$o-cv.dump
done
