set -ex
cp "$TESTDIR/split-file-scope-variables.cpp" hooks.cpp

# Clang 20 describes every file-scope variable before the first procedure. `host::calls` makes
# Clang describe namespace `host` first, so the DIE of `host::run`, a child of that namespace,
# comes before the DIE of `hooks`.
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -S -o hooks.s hooks.cpp
awk 'FNR == NR && $1 ~ /^\.Linfo_string[0-9]+:$/ { l = substr($1, 1, length($1) - 1); getline; s = $2; gsub(/"/, "", s); str[l] = s; next }
     FNR == NR && $1 == ".section" { inoffs = $2 ~ /^\.debug_str_offsets/; next }
     FNR == NR && inoffs && $1 == ".secrel32" && $2 ~ /^\.Linfo_string[0-9]+$/ { lab[n++] = $2; next }
     FNR == NR { next }
     function die() {
       if (tag == "DW_TAG_subprogram") proc = name
       if (tag == "DW_TAG_subprogram" && name == "run") run = 1
       pieces = ops ~ /^( 161 [0-9]+ 147 [0-9]+)( 161 [0-9]+ 147 [0-9]+)+$/
       if (tag == "DW_TAG_variable" && name == "hooks" && run && pieces) ok = 1
       if (tag == "DW_TAG_variable" && name == "table" && proc == "run_table" && pieces) staticok = 1
       tag = ""; name = ""; ops = ""
     }
     NF != 2 { loc = 0 }
     loc && $1 == ".byte" { ops = ops " " $2; loc--; next }
     /# Abbrev \[/ { die(); tag = $NF; next }
     /# End Of Children Mark$/ { die(); next }
     $NF == "DW_AT_name" { name = str[lab[$2]] }
     tag == "DW_TAG_variable" && $NF == "DW_AT_location" { loc = $2 }
     END { die(); exit !(ok && staticok) }' hooks.s hooks.s ||
  { echo "test precondition lost: the DWARF does not describe the file-scope variable 'hooks' after the procedure host::run, does not locate 'hooks' in several DW_OP_addrx pieces, or does not locate the static variable 'table' of run_table in several DW_OP_addrx pieces"; exit 1; }
clang --target=x86_64-w64-mingw32 -g -gdwarf-5 -O2 -c -o hooks.o hooks.cpp
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o hooks.exe hooks.o
"$CV2PDB" hooks.exe hooks-cv.exe
"$CVDUMP" -s hooks-cv.pdb >hooks-cv.dump

# The file-scope variable `hooks` and the static variable `table` of `run_table` get no S_LOCAL
# record in any procedure, and `host::run` keeps the record of its local `s`.
awk '$2 == "S_GPROC32:" { proc = $NF; next }
     $2 == "S_LOCAL:" && ($NF == "hooks" || $NF == "table") { print; bad = 1 }
     $2 == "S_LOCAL:" && $NF == "s" && proc == "host::run" { print; found = 1 }
     END { exit !(found && !bad) }' hooks-cv.dump
