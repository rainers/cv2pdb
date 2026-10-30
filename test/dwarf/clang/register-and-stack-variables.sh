set -ex
cp "$TESTDIR/register-and-stack-variables.c" vars.c

clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -S -o vars.s vars.c
awk 'FNR == NR && $1 ~ /^\.Linfo_string[0-9]+:$/ { l = substr($1, 1, length($1) - 1); getline; s = $2; gsub(/"/, "", s); str[l] = s; next }
     FNR == NR && $1 == ".section" { inoffs = $2 ~ /^\.debug_str_offsets/; inabbrev = $2 ~ /^\.debug_abbrev/; next }
     FNR == NR && inoffs && $1 == ".secrel32" && $2 ~ /^\.Linfo_string[0-9]+$/ { lab[n++] = $2; next }
     FNR == NR && inabbrev && /# Abbreviation Code$/ { code = $2; next }
     FNR == NR && inabbrev && $NF == "DW_AT_location" { getline; if ($NF == "DW_FORM_loclistx" || $NF == "DW_FORM_sec_offset") list[code] = 1; next }
     FNR == NR { next }
     function die() {
       if (tag == "DW_TAG_subprogram")
         inmixed = name == "mixed" && fb == " 87"
       else if (inmixed && islist) {
         lists = 1
         if (tag == "DW_TAG_variable") reglocal = 1
       } else if (inmixed && lists && ops ~ /^ 145( [0-9]+)+$/) {
         k = split(ops, o, " "); v = 0
         for (i = 2; i <= k; i++) v += (o[i] % 128) * 2^(7 * (i - 2))
         if (o[k] >= 64) v -= 2^(7 * (k - 1))
         if (tag == "DW_TAG_formal_parameter") { print name, v, "Param:"; stackparam = 1 }
         if (tag == "DW_TAG_variable") { print name, v, "Local:"; stacklocal = 1 }
       }
       tag = ""; name = ""; ops = ""; fb = ""; islist = 0
     }
     NF != 2 { loc = 0; fbloc = 0 }
     loc && $1 == ".byte" { ops = ops " " $2; loc--; next }
     fbloc && $1 == ".byte" { fb = fb " " $2; fbloc--; next }
     /# Abbrev \[/ { die(); tag = $NF; islist = list[$2]; next }
     /# End Of Children Mark$/ { die(); next }
     $NF == "DW_AT_name" { name = str[lab[$2]] }
     $NF == "DW_AT_frame_base" { fbloc = $2 }
     $NF == "DW_AT_location" && !islist { loc = $2 }
     END { die(); exit !(reglocal && stackparam && stacklocal) }' vars.s vars.s >vars.expected ||
  { echo "test precondition lost: mixed() has no frame base in rsp (DW_OP_reg7), no local with a location list, no parameter at DW_OP_fbreg after a variable with a location list, or no local at DW_OP_fbreg after a variable with a location list"; exit 1; }
cat vars.expected
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -c -o vars.o vars.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o vars.exe vars.o
"$CV2PDB" vars.exe vars-cv.exe
"$CVDUMP" -s vars-cv.pdb >vars-cv.dump

# No scope has an S_REGREL32, S_BPREL32 or S_REGISTER record after an S_LOCAL record.
awk '$2 ~ /^S_[GL]PROC32:$/ || $2 == "S_BLOCK32:" { d++; seen[d] = 0; next }
     $2 == "S_END" { if (d) d--; next }
     $2 == "S_LOCAL:" { seen[d] = 1 }
     ($2 == "S_REGREL32:" || $2 == "S_BPREL32:" || $2 == "S_REGISTER:") && seen[d] { print; bad = 1 }
     END { exit bad }' vars-cv.dump

# Each variable of mixed() at DW_OP_fbreg has an S_LOCAL record that marks it as a parameter or a
# local, as the DWARF does, and a whole-variable record at rsp plus its offset.
awk 'FNR == NR { want[$1] = $2; kind[$1] = $3; next }
     $2 == "S_GPROC32:" { inmixed = $NF == "mixed"; next }
     $2 == "S_LOCAL:" { var = inmixed ? $NF : ""; if ((var in want) && $3 != kind[var]) { print; bad = 1 } next }
     (var in want) && $2 == "S_DEFRANGE_REGISTER_REL:" {
       print var, $0
       off = strtonum("0x" $5); if (off >= 2^31) off -= 2^32
       if ($3 == "[rsp" && off == want[var] && NF == 6) found[var] = 1
     }
     $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
     END { for (v in want) if (!(v in found)) { print "variable " v " has no whole-variable record at rsp+" want[v]; bad = 1 } exit bad }' vars.expected vars-cv.dump
