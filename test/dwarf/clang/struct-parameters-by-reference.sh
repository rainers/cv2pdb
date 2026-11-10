set -ex
cp "$TESTDIR/struct-parameters-by-reference.c" byval.c

for v in 2 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -ffunction-sections -S -o byval-d$v.s byval.c
  awk -v v=$v '/# DW_AT_location$/ { getline; form[$NF] = 1 }
       /^\.Ldebug_loc[0-9]+:$/ { getline; if ($1 == ".quad" && $2 == "-1") base = 1 }
       /# DW_LLE_base_addressx$/ { base = 1 }
       END { exit !(base && (v == 2 ? form["DW_FORM_data4"] : v == 4 ? form["DW_FORM_sec_offset"] : 1)) }' byval-d$v.s ||
    { echo "test precondition lost: byval-d$v.s has no location list that starts with a base address entry, or DW_AT_location is not data4 (DWARF 2) or sec_offset (DWARF 4)"; exit 1; }
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -ffunction-sections -c -o byval-d$v.o byval.c
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o byval-d$v.exe byval-d$v.o
  llvm-objdump -p byval-d$v.exe >byval-d$v.headers
  awk '$1 == "ImageBase" { found = strtonum("0x" $2) >= 2^32 } END { exit !found }' byval-d$v.headers || { echo "test precondition lost: image base below 4 GB"; exit 1; }
  llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=byval byval-d$v.o >byval-d$v.vars
  grep -E '[/|]- p = \[RCX\]' byval-d$v.vars || { echo "test precondition lost: the DWARF does not locate parameter 'p' at [RCX]"; exit 1; }
  grep -E '[/|]- p = \[RSI\]' byval-d$v.vars || { echo "test precondition lost: the DWARF does not locate parameter 'p' at [RSI]"; exit 1; }
  "$CV2PDB" byval-d$v.exe byval-cv-d$v.exe
  "$CVDUMP" -s byval-cv-d$v.pdb >byval-cv-d$v.dump

  # The 16-byte struct `p` gets one S_LOCAL record without the parameter flag. The whole-variable
  # records of `p` are at [rcx] from the start of byval(), and at [rsi].
  awk '$2 == "S_GPROC32:" { inp = $NF == "byval"; split($3, a, /[:\]]/); lo = a[2]; var = ""; next }
       !inp { next }
       $2 == "S_LOCAL:" { var = $NF; if (var == "p") { print; n++; kind = $3 } next }
       var == "p" && $2 == "S_DEFRANGE_REGISTER_REL:" && $5 == "0000" && NF == 6 { print; reg = $3; next }
       var == "p" && $2 ~ /^S_DEFRANGE_/ { print; bad = 1; reg = ""; next }
       var == "p" && $1 == "Range:" { print; split($2, s, /[:\]]/); if (reg == "[rcx" && s[2] == lo) entry = 1; if (reg == "[rsi") later = 1 }
       $2 ~ /^S_/ { var = "" }
       END { exit !(n == 1 && kind == "Local:" && entry && later && !bad) }' byval-cv-d$v.dump
done
