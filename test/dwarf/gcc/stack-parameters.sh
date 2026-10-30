set -ex
cp "$TESTDIR/stack-parameters.c" stacked.c

for v in 4 5; do
  gcc -g -gdwarf-$v -O2 -dA -S -o stacked-d$v.s stacked.c
  grep -q DW_OP_call_frame_cfa stacked-d$v.s || { echo "test precondition lost: frame base is not the CFA"; exit 1; }
  awk 'FNR == NR && $2 == "\"e\\0\"" && $NF == "DW_AT_name" { name = 1; next }
       FNR == NR && name && $NF == "DW_AT_location" { list = $2 ":"; name = 0; next }
       FNR == NR { next }
       $1 == list { inlist = 1; next }
       inlist && $NF ~ /^DW_OP_/ { if ($NF == "DW_OP_fbreg") { getline; print $2 } exit }' stacked-d$v.s stacked-d$v.s >stacked-d$v.fbreg
  test -s stacked-d$v.fbreg || { echo "test precondition lost: the location list of parameter 'e' does not start with DW_OP_fbreg"; exit 1; }
  gcc -g -gdwarf-$v -O2 -o stacked-d$v.exe stacked.c
  objdump -d --no-show-raw-insn --disassemble=stacked stacked-d$v.exe >stacked-d$v.asm
  awk '/^ +[0-9a-f]+:/ { a = strtonum("0x" substr($1, 1, length($1) - 1)); if (!n++) { start = a; if (match($0, /sub +\$0x([0-9a-f]+),%rsp$/, m)) frame = strtonum("0x" m[1]) } else { print frame, a - start; exit } }' stacked-d$v.asm >stacked-d$v.frame
  awk '$1 > 0 { found = 1 } END { exit !found }' stacked-d$v.frame || { echo "test precondition lost: stacked() does not start by subtracting its frame size from rsp"; exit 1; }
  "$CV2PDB" stacked-d$v.exe stacked-cv-d$v.exe
  "$CVDUMP" -s stacked-cv-d$v.pdb >stacked-cv-d$v.dump

  # The CFA is rsp+8 at the entry of stacked(). The DWARF places the parameter `e` at an offset from
  # the CFA, so `e` is at rsp+8+offset until the sub instruction allocates the frame. After that
  # instruction, `e` is at rsp+8+offset+frame size.
  awk -v off=$(cat stacked-d$v.fbreg) -v frame=$(cut -d' ' -f1 stacked-d$v.frame) -v after=$(cut -d' ' -f2 stacked-d$v.frame) \
      '$2 == "S_GPROC32:" { inp = $NF == "stacked"; split($3, a, /[:\]]/); if (inp) lo = strtonum("0x" a[2]); var = ""; next }
       !inp { next }
       $2 == "S_LOCAL:" { var = $NF; if (var ~ /^[a-e]$/) { print; n[var]++; if ($3 != "Param:") bad = 1 } next }
       var == "e" && $2 ~ /^S_DEFRANGE_/ { print; slot = $2 == "S_DEFRANGE_REGISTER_REL:" && $3 == "[rsp" && NF == 6 ? strtonum("0x" $5) : -1; next }
       var == "e" && $1 == "Range:" { print; split($2, s, /[:\]]/); split($4, x, /[:\]]/); at[strtonum("0x" s[2])] = slot " " strtonum("0x" x[2]) }
       $2 ~ /^S_/ { var = "" }
       END {
         printf "expected rsp+%x up to offset %x, then rsp+%x\n", off + 8, after, off + 8 + frame
         for (p in n) if (n[p] != 1) bad = 1
         exit !(length(n) == 5 && at[lo] == (off + 8) " " (lo + after) && at[lo + after] ~ "^" (off + 8 + frame) " " && !bad)
       }' stacked-cv-d$v.dump
done
