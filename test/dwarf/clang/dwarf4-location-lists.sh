set -ex
cp "$TESTDIR/dwarf4-location-lists.c" spill.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-4 -O2 -ffunction-sections -o spill.exe spill.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-4 -O2 -ffunction-sections -S -o spill.s spill.c
awk 'FNR == NR && $1 ~ /^\.Linfo_string[0-9]+:$/ { s = substr($1, 1, length($1) - 1); getline; str[s] = $2; next }
     FNR == NR && $1 == ".section" { inloc = $2 ~ /^\.debug_loc,/; next }
     FNR == NR && inloc && $1 ~ /^\.Ldebug_loc[0-9]+:$/ { l = substr($1, 1, length($1) - 1); getline; base[l] = $1 == ".quad" && $2 == "-1"; next }
     FNR == NR && inloc && $NF == "DW_OP_breg7" { getline; slots[l] = slots[l] " " $NF; next }
     FNR == NR && inloc && $NF ~ /^DW_OP_reg[0-9]+$/ { regs[l] = 1; next }
     FNR == NR { next }
     /Abbrev \[/ { tag = $NF; if (tag == "DW_TAG_subprogram") insub = 0 }
     tag == "DW_TAG_subprogram" && $NF == "DW_AT_name" { insub = str[$2] == "\"spill\"" }
     insub && tag == "DW_TAG_formal_parameter" && $NF == "DW_AT_location" { l = $2 }
     insub && tag == "DW_TAG_formal_parameter" && $NF == "DW_AT_name" { if (!base[l] || slots[l] == "" || !regs[l]) bad = 1; name = str[$2]; gsub(/"/, "", name); print name slots[l]; n++; l = "" }
     END { exit bad || n != 2 }' spill.s spill.s >spill.expected ||
  { echo "test precondition lost: spill() does not have two parameters whose location lists start with a base address entry and include a register and an rsp-relative location"; exit 1; }
cat spill.expected
"$CV2PDB" spill.exe spill-cv.exe
"$CVDUMP" -s spill-cv.pdb >spill-cv.dump

# Each parameter has one S_LOCAL record, with a range in a 32-bit register and one in its stack
# slot, and every range lies inside spill().
awk 'FNR == NR { want[$1] = $0 " "; next }
     $2 == "S_GPROC32:" { inspill = $NF == "spill"; d = 0; var = ""; split($3, a, /[:\]]/); lo = strtonum("0x" a[2]); hi = lo + strtonum("0x" substr($5, 1, 8)); next }
     !inspill { next }
     $2 == "S_BLOCK32:" { d++ }
     $2 == "S_END" { if (d) d--; else inspill = 0 }
     $2 == "S_REGREL32:" && ($NF in want) { print; bad = 1 }
     $2 == "S_LOCAL:" { var = $NF; if (var in want) { got[var]++; if ($3 != "Param:") { print; bad = 1 } } next }
     $2 == "S_DEFRANGE_REGISTER_REL:" && (var in want) && $3 == "[rsp" && index(want[var], " " strtonum("0x" $5) " ") { slot[var] = 1 }
     $2 == "S_DEFRANGE_REGISTER:" && (var in want) && $3 ~ /^(e(ax|bx|cx|dx|si|di|bp)|r([89]|1[0-5])d)$/ { reg[var] = 1 }
     $1 == "Range:" {
       split($2, s, /[:\]]/); split($4, e, /[:\]]/); rs = strtonum("0x" s[2]); re = strtonum("0x" e[2])
       if (rs < lo || re > hi || re <= rs || re - rs > 61440) { print; bad = 1 }
       for (i = 9; i < NF; i += 2) { gs = $i; gl = $(i + 1); gsub(/[(),]/, "", gs); gsub(/[(),]/, "", gl); if (strtonum("0x" gs) + strtonum("0x" gl) > re - rs) { print; bad = 1 } }
     }
     $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
     END { for (p in want) if (got[p] != 1 || !slot[p] || !reg[p]) { print "parameter " p " has no single S_LOCAL Param record with both a stack slot and a 32-bit register"; bad = 1 }; exit bad }' spill.expected spill-cv.dump
