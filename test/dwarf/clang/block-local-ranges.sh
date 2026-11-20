set -ex
cp "$TESTDIR/block-local-ranges.c" blocks.c

printf '1 count\n1 late\n1 cold_slot\n1 count\n1 late\n' >blocks.expected
for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O2 -o blocks-d$v.exe blocks.c
  "$CV2PDB" blocks-d$v.exe blocks-cv-d$v.exe
  "$CVDUMP" -s blocks-cv-d$v.pdb >blocks-cv-d$v.dump

  # In `target()` the unlikely call that uses `count` is placed after the block of cold_slot, so the
  # block of `count` is written as two S_BLOCK32 records. `count` has a location in both records, and
  # `late` has one only in the first record.
  awk '$2 == "S_GPROC32:" { fn = $NF; d = 0 }
       fn != "target" { next }
       $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; lo[d] = strtonum("0x" a[2]); hi[d] = lo[d] + strtonum("0x" substr($5, 1, 8)) }
       $2 == "S_END" { if (d) d--; else fn = "" }
       $2 == "S_LOCAL:" { var = $NF; next }
       $1 == "Range:" && d {
         split($2, s, /[:\]]/); split($4, e, /[:\]]/)
         if (strtonum("0x" s[2]) < hi[d] && strtonum("0x" e[2]) > lo[d]) blocks[var, lo[d]] = 1
       }
       $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
       END { for (k in blocks) { split(k, p, SUBSEP); n[p[1]]++ } exit !(n["count"] == 2 && n["late"] == 1) }' blocks-cv-d$v.dump ||
    { echo "test precondition lost: in target(), 'count' should have a location in exactly two S_BLOCK32 records, and 'late' should have a location in exactly one"; exit 1; }

  # Each piece of the block of `count` has an S_LOCAL record for each variable of that block,
  # including `late` in the piece where `late` has no location.
  awk '$2 == "S_GPROC32:" { fn = $NF; d = 0 }
       fn != "target" { next }
       $2 == "S_BLOCK32:" { d++ }
       $2 == "S_LOCAL:" && d { print d, $NF }
       $2 == "S_END" { if (d) d--; else fn = "" }' blocks-cv-d$v.dump >blocks-cv-d$v.locals
  diff blocks.expected blocks-cv-d$v.locals

  # A debugger reads a variable only in the scope that contains the PC, so every S_DEFRANGE_* record
  # in an S_BLOCK32 scope lies inside the range of that scope.
  awk '$2 ~ /^S_[GL]PROC32:$/ { fn = $NF; d = 0 }
       $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; lo[d] = strtonum("0x" a[2]); hi[d] = lo[d] + strtonum("0x" substr($5, 1, 8)) }
       $2 == "S_END" && d { d-- }
       $2 == "S_LOCAL:" { var = $NF }
       $1 == "Range:" && d {
         split($2, s, /[:\]]/); split($4, e, /[:\]]/)
         if (strtonum("0x" s[2]) < lo[d] || strtonum("0x" e[2]) > hi[d]) {
           printf "%s: the range of %s, %s - %s, is outside its S_BLOCK32 [%x, %x)\n", fn, var, s[2], e[2], lo[d], hi[d] >"/dev/stderr"
           bad = 1
         }
       }
       END { exit bad }' blocks-cv-d$v.dump
done
