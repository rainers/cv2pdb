set -ex
cp "$TESTDIR/out-of-line-instances.c" inline.c

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -S -o inline-d$v.s inline.c
  awk '/^\t\.section/ { abbrev = $2 ~ /^\.debug_abbrev/ }
       !abbrev { next }
       /# Abbreviation Code$/ { tag = ""; delete at }
       /# DW_TAG_/ && !tag { tag = $NF }
       /# DW_AT_/ { at[$NF] = 1 }
       /# EOM\(1\)$/ {
         if (tag == "DW_TAG_subprogram" && ("DW_AT_abstract_origin" in at) && ("DW_AT_low_pc" in at)) sub_ok = 1
         if (tag == "DW_TAG_inlined_subroutine" && ("DW_AT_abstract_origin" in at)) inl_ok = 1
         if (("DW_AT_abstract_origin" in at) && ("DW_AT_location" in at) && !("DW_AT_name" in at)) {
           if (tag == "DW_TAG_formal_parameter") par_ok = 1
           if (tag == "DW_TAG_variable") var_ok = 1
         }
       }
       END {
         if (!sub_ok) print "test precondition lost: no DW_TAG_subprogram with DW_AT_abstract_origin and DW_AT_low_pc"
         if (!inl_ok) print "test precondition lost: no DW_TAG_inlined_subroutine with DW_AT_abstract_origin"
         if (!par_ok) print "test precondition lost: no DW_TAG_formal_parameter with DW_AT_location and no DW_AT_name"
         if (!var_ok) print "test precondition lost: no DW_TAG_variable with DW_AT_location and no DW_AT_name"
         exit !(sub_ok && inl_ok && par_ok && var_ok)
       }' inline-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O2 -o inline-d$v.exe inline.c
  llvm-objdump -d --no-show-raw-insn --disassemble-symbols=helper inline-d$v.exe >inline-d$v.asm
  "$CV2PDB" inline-d$v.exe inline-cv-d$v.exe
  "$CVDUMP" -s inline-cv-d$v.pdb >inline-cv-d$v.dump
  awk 'FNR == NR { if (match($0, /0x[0-9a-f]+\(%rsp\), %rcx$/)) slots[strtonum(substr($0, RSTART, RLENGTH - 12))] = 1; next }
       $2 == "S_GPROC32:" { fn = $NF; d = 0 }
       fn != "" && $2 == "S_BLOCK32:" { d++ }
       $2 == "S_END" { if (d) d--; else fn = "" }
       $2 == "S_LOCAL:" { var = $NF; if (fn == "main" && (var == "a" || var == "local")) { print; leaked = 1 } next }
       fn == "helper" && $2 == "S_DEFRANGE_REGISTER_REL:" { print var, $0; if ($3 == "[rsp" && (strtonum("0x" $5) in slots)) found[var] = 1 }
       $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
       END { exit !(("a" in found) && ("local" in found) && !leaked) }' inline-d$v.asm inline-cv-d$v.dump
done
