set -ex
cp "$TESTDIR/arm64-location-lists.c" locals.c

clang --target=aarch64-w64-mingw32 -g -O2 -c -o locals.o locals.c
clang --target=aarch64-w64-mingw32 -fuse-ld=lld -nostdlib -Wl,-e,start -o locals.exe locals.o
llvm-objdump -f locals.exe | grep 'file format coff-arm64' || { echo "test precondition lost: locals.exe is not an ARM64 image"; exit 1; }
llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=spill locals.o >locals.vars
awk 'match($0, /[\/|]- ([ab]) = W[0-9]+ *$/, m) { w[m[1]] = 1 }
     match($0, /[\/|]- ([ab]) = \[WSP\+([0-9]+)\]/, m) { slot[m[1]] = m[2] }
     END { if (!(w["a"] && w["b"] && ("a" in slot || "b" in slot))) exit 1; for (p in w) print p, (p in slot) ? slot[p] : "-" }' locals.vars >locals.expected ||
  { echo "test precondition lost: the DWARF does not locate both parameters of spill() in a W register and one of them in a stack slot"; exit 1; }
cat locals.expected
"$CV2PDB" locals.exe locals-cv.exe
"$CVDUMP" -s locals-cv.pdb >locals-cv.dump

# The parameters `a` and `b` of spill() each get one S_LOCAL record with the parameter flag. Each
# parameter is in a W register from the start of spill(), and in the stack slot that the DWARF gives
# for it, if the DWARF gives one.
awk 'function arm(r) { return r ~ /^([wx]([0-9]|[12][0-9]|30)|sp|fp|lr)$/ }
     FNR == NR { want[$1] = $2; next }
     $2 == "S_GPROC32:" { inp = $NF == "spill"; split($3, a, /[:\]]/); lo = a[2]; var = ""; next }
     $2 == "S_DEFRANGE_REGISTER:" && !arm($3) { print; bad = 1 }
     $2 == "S_DEFRANGE_REGISTER_REL:" && !arm(substr($3, 2)) { print; bad = 1 }
     $2 == "S_DEFRANGE_SUBFIELD_REGISTER:" && !arm($6) { print; bad = 1 }
     !inp { next }
     $2 == "S_LOCAL:" { var = $NF; if (var in want) { print; n[var]++; if ($3 != "Param:") bad = 1 } next }
     (var in want) && $2 ~ /^S_DEFRANGE_/ { print; rec = $2 == "S_DEFRANGE_REGISTER:" && $3 ~ /^w/ ? "w" : $2 == "S_DEFRANGE_REGISTER_REL:" && $3 == "[sp" && NF == 6 ? "sp+" strtonum("0x" $5) : ""; next }
     (var in want) && $1 == "Range:" { print; split($2, s, /[:\]]/); if (rec == "w" && s[2] == lo) entry[var] = 1; if (rec == "sp+" want[var]) stack[var] = 1 }
     $2 ~ /^S_/ { var = "" }
     END { for (p in want) if (n[p] != 1 || !entry[p] || (want[p] != "-" && !stack[p])) bad = 1; exit bad }' locals.expected locals-cv.dump
