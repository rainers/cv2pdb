set -ex
cp "$TESTDIR/void-pointers.c" void.c
printf '%s\n' 'g_vp: void *' 'g_cvp: const void *' 'g_tvp: void *' 'payload: void *' >void.expected

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o void$v.exe void.c
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o void$v.s void.c
  awk 'function die() { if (!typed) bare[tag]++ }
       /# Abbrev \[[0-9]+\] / { die(); tag = $NF; typed = 0; next }
       $NF == "DW_AT_type" { typed = 1 }
       END {
         die()
         if (!bare["DW_TAG_pointer_type"]) print "test precondition lost: no DW_TAG_pointer_type without DW_AT_type"
         if (!bare["DW_TAG_const_type"]) print "test precondition lost: no DW_TAG_const_type without DW_AT_type"
         if (!bare["DW_TAG_typedef"]) print "test precondition lost: no DW_TAG_typedef without DW_AT_type"
         exit !(bare["DW_TAG_pointer_type"] && bare["DW_TAG_const_type"] && bare["DW_TAG_typedef"])
       }' void$v.s
  "$CV2PDB" void$v.exe void$v-cv.exe
  "$CVDUMP" -t void$v-cv.pdb >void$v-cv.types
  "$CVDUMP" -g void$v-cv.pdb >void$v-cv.globals
  # The awk program below prints the C spelling of the type of each pointer global and of the
  # payload member.
  awk 'function spell(t) {
         if (t == "T_VOID(0003)") return "void"
         if (t == "T_64PVOID(0603)") return "void *"
         if (t in pointee) return spell(pointee[t]) " *"
         if (t in modified) return qualifier[t] spell(modified[t])
         return t
       }
       FNR == NR && $2 == ":" && $3 == "Length" { idx = toupper($1); leaf = $NF; next }
       FNR == NR && leaf == "LF_POINTER" && $1 == "Element" { pointee[idx] = toupper($4); next }
       FNR == NR && leaf == "LF_MODIFIER" && NF > 1 && $(NF - 1) == "type" { modified[idx] = toupper($NF); qualifier[idx] = $1 == "const," ? "const " : ""; next }
       FNR == NR && $3 == "LF_MEMBER," { member = toupper($7); sub(/,$/, "", member); next }
       FNR == NR && $0 ~ /member name = .payload.$/ { print "payload: " spell(member); next }
       FNR == NR { next }
       $1 == "S_GDATA32:" && $NF ~ /^g_(vp|cvp|tvp)$/ { t = toupper($4); sub(/,$/, "", t); print $NF ": " spell(t) }' void$v-cv.types void$v-cv.globals >void$v.got
  sort void.expected >void$v.want
  sort void$v.got >void$v.sorted
  diff void$v.want void$v.sorted
done
