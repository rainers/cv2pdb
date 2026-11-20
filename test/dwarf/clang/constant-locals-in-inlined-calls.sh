set -ex
cp "$TESTDIR/constant-locals-in-inlined-calls.c" across.c

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O2 -c -o across-d$v.o across.c
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -o across-d$v.exe across-d$v.o
  llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=across across-d$v.o >across-d$v.objdump
  expand across-d$v.objdump >across-d$v.vars
  awk -v var=k -f "$TESTDIR/objdump-var-ranges.awk" across-d$v.vars >across-d$v.k
  awk -v var=doubled -f "$TESTDIR/objdump-var-ranges.awk" across-d$v.vars >across-d$v.doubled
  cat across-d$v.k across-d$v.doubled
  awk 'FNR == NR { if ($3 == "lit") { clo[++nc] = $1; chi[nc] = ($2 == "end" ? 65536 : $2) } else reg = 1; next }
       { for (i = 1; i <= nc; i++) if (clo[i] < ($2 == "end" ? 65536 : $2) && $1 < chi[i]) found = 1 }
       END { exit !(nc && !reg && found) }' across-d$v.k across-d$v.doubled ||
    { echo "test precondition lost: 'k' in across() is not only ever a DW_OP_lit<n> constant, or no constant range of 'k' overlaps a range of 'doubled' from the inlined call of twice()"; exit 1; }
  "$CV2PDB" across-d$v.exe across-cv-d$v.exe
  "$CVDUMP" -s across-cv-d$v.pdb >across-cv-d$v.dump

  # No two S_BLOCK32 records of across() with the same parent overlap. An S_BLOCK32 record with an
  # S_LOCAL record for `doubled` is the scope of an inlined call of twice(). Every address in a
  # constant range of `k` is covered by exactly one S_CONSTANT record for `k`, with the value of that
  # range, and inside the scope of an inlined call that record is nested in that scope. No
  # S_CONSTANT record for `k` covers an address outside the constant ranges of `k`.
  awk 'FNR == NR { if ($3 == "lit") for (o = $1; o < ($2 == "end" ? 65536 : $2); o++) want[o] = $4; next }
       $2 == "S_GPROC32:" { inp = $NF == "across"; d = 0; ns[1] = 0; if (!inp) next; split($3, a, /[:\]]/); base = strtonum("0x" a[2]); size = strtonum("0x" substr($5, 1, 8)); next }
       !inp { next }
       $2 == "S_BLOCK32:" {
         split($3, a, /[:\]]/); d++; blo[d] = strtonum("0x" a[2]) - base; bhi[d] = blo[d] + strtonum("0x" substr($5, 1, 8)); inl[d] = 0
         for (i = 1; i <= ns[d]; i++)
           if (slo[d, i] < bhi[d] && blo[d] < shi[d, i]) { printf "sibling S_BLOCK32 records [%d, %d) and [%d, %d) overlap\n", slo[d, i], shi[d, i], blo[d], bhi[d]; bad = 1 }
         ns[d]++; slo[d, ns[d]] = blo[d]; shi[d, ns[d]] = bhi[d]; ns[d + 1] = 0
         next
       }
       $2 == "S_END" { if (d) d--; else inp = 0; next }
       $2 == "S_LOCAL:" && $NF == "doubled" && d { inl[d] = 1; calls++; for (o = blo[d]; o < bhi[d]; o++) incall[o] = 1; next }
       $2 == "S_CONSTANT:" && $NF == "k" {
         print
         v = $(NF - 1); sub(/,$/, "", v)
         nested = 0; for (j = 1; j <= d; j++) if (inl[j]) nested = 1
         for (o = d ? blo[d] : 0; o < (d ? bhi[d] : size); o++) { got[o]++; val[o] = v; if (nested) gotin[o]++ }
       }
       END {
         if (!calls) { print "no S_BLOCK32 record of across() has an S_LOCAL record for `doubled`"; exit 1 }
         for (o = 0; o < size; o++) {
           if (!(o in want)) { if (got[o]) { print "offset " o ": " got[o] " S_CONSTANT records for `k`, want 0 because no constant range of `k` covers it"; bad = 1 } }
           else if (got[o] != 1 || val[o] != want[o]) { print "offset " o ": " got[o] + 0 " S_CONSTANT records for `k`, value " val[o] ", want " want[o]; bad = 1 }
           else if ((o in incall) && !gotin[o]) { print "offset " o ": the S_CONSTANT record for `k` is not nested in the scope of the inlined call that covers it"; bad = 1 }
         }
         exit bad
       }' across-d$v.k across-cv-d$v.dump
done
