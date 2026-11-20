set -ex
cp "$TESTDIR/inlined-subroutine-locals.c" inline.c

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -S -o inline-d$v.s inline.c
  # The parameter of offset_of() is a constant and gets no S_LOCAL record, so the scope of its
  # inlined call has no symbols and must not be written.
  awk '/^\t\.section/ { abbrev = $2 ~ /^\.debug_abbrev/ }
       !abbrev { next }
       /# Abbreviation Code$/ { tag = ""; delete at }
       /# DW_TAG_/ && !tag { tag = $NF }
       /# DW_AT_/ { at[$NF] = 1 }
       /# EOM\(1\)$/ {
         if (tag == "DW_TAG_inlined_subroutine") inl_ok = 1
         if (tag == "DW_TAG_formal_parameter" && ("DW_AT_abstract_origin" in at) && ("DW_AT_const_value" in at) && !("DW_AT_location" in at)) const_ok = 1
       }
       END {
         if (!inl_ok) print "test precondition lost: no DW_TAG_inlined_subroutine"
         if (!const_ok) print "test precondition lost: the parameter of offset_of() has a location, or no DW_AT_const_value"
         exit !(inl_ok && const_ok)
       }' inline-d$v.s
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -c -o inline-d$v.o inline.c
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o inline-d$v.exe inline-d$v.o
  # llvm-objdump lists each variable that has a location in main(), its own parameters included.
  llvm-objdump -d --no-show-raw-insn --debug-vars --disassemble-symbols=main inline-d$v.o >inline-d$v.vars
  "$CV2PDB" inline-d$v.exe inline-cv-d$v.exe
  "$CVDUMP" -s inline-cv-d$v.pdb >inline-cv-d$v.dump

  # Each inlined-call variable that llvm-objdump lists with a location has an S_LOCAL record in an
  # S_BLOCK32 scope of main(), with ranges inside that scope. Every S_BLOCK32 scope of main()
  # contains such a record, directly or in a nested scope.
  awk 'FNR == NR {
         if (match($0, /[^ ]+ [A-Za-z_][A-Za-z0-9_]* = /)) { name = substr($0, RSTART, RLENGTH); sub(/^[^ ]+ /, "", name); sub(/ = $/, "", name); located[name] = 1 }
         next
       }
       $2 == "S_GPROC32:" { fn = $NF; d = 0; args = 1 }
       fn != "main" { next }
       $2 == "S_ENDARG" { args = 0 }
       $2 == "S_LOCAL:" { var = $NF; if (args) own[var] = 1; else if (d) { inblock[var] = 1; if ((var in located) && !(var in own)) has[d] = 1 } next }
       $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; lo[d] = strtonum("0x" a[2]); hi[d] = lo[d] + strtonum("0x" substr($5, 1, 8)); has[d] = 0 }
       $1 == "Range:" && d {
         split($2, s, /[:\]]/); split($4, e, /[:\]]/)
         if (strtonum("0x" s[2]) < lo[d] || strtonum("0x" e[2]) > hi[d]) { printf "the range of %s, %s - %s, is outside its S_BLOCK32 [%x, %x)\n", var, s[2], e[2], lo[d], hi[d] >"/dev/stderr"; bad = 1 }
       }
       $2 == "S_END" {
         if (!d) { fn = ""; next }
         if (!has[d]) { printf "the S_BLOCK32 at %x has no variable of an inlined call with a location\n", lo[d] >"/dev/stderr"; bad = 1 }
         d--; if (d) has[d] = has[d] || has[d + 1]
       }
       $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
       END {
         if (!("factor" in located) || !("scaled" in located)) { print "test precondition lost: `factor` or `scaled` has no location in main()"; exit 1 }
         for (v in located) if (!(v in own) && !(v in inblock)) { print "variable " v " has no S_LOCAL record in an S_BLOCK32 scope of main()" >"/dev/stderr"; bad = 1 }
         exit bad
       }' inline-d$v.vars inline-cv-d$v.dump
done
