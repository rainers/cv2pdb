set -ex
cp "$TESTDIR/dwarf5-program.go" main.go
mkdir -p functype
cp "$TESTDIR/dwarf5-program-functype.go" functype/main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o go-dwarf.exe main.go
"$CV2PDB" go-dwarf.exe go-cv.exe
"$CVDUMP" -g go-cv.pdb >go-cv.globals
"$CVDUMP" -t go-cv.pdb >go-cv.types

# The global record of a func variable has a pointer to a function pointer as its type.
shape=$(go run functype/main.go go-dwarf.exe main.sink)
test -n "$shape" || { echo "test precondition lost: no DWARF variable named main.sink"; exit 1; }
test "$shape" = "Typedef SubroutineType:8" || { echo "test precondition lost: the DWARF type of main.sink is $shape, not a typedef of an 8-byte subroutine type"; exit 1; }
sink=$(awk '$1 == "S_GDATA32:" && $NF == "main@sink" { t = $4; sub(/,$/, "", t); print tolower(t) }' go-cv.globals)
test -n "$sink" || { echo "test precondition lost: no global record for main.sink"; exit 1; }
awk -v sink="$sink" 'function render(t,  k, n, i, a, s) {
       if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
       k = tolower(t)
       if (leaf[k] == "LF_MODIFIER") return render(base[k])
       if (leaf[k] == "LF_POINTER") return "ptr" size[k] "(" render(base[k]) ")"
       if (leaf[k] == "LF_PROCEDURE") {
         n = split(args[tolower(arglist[k])], a, " ")
         for (i = 1; i <= n; i++) s = s (i > 1 ? "," : "") render(a[i])
         return "proc " render(rvtype[k]) "(" s ")"
       }
       return leaf[k] "?" t
     }
     /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $9; next }
     /modifies type/ { base[id] = $NF; next }
     /Pointer \(/ { size[id] = $NF; next }
     /Element type :/ { base[id] = $NF; next }
     /Return type = / { rvtype[id] = $4; sub(/,$/, "", rvtype[id]); next }
     /Arg list type = / { arglist[id] = $NF; next }
     leaf[id] == "LF_ARGLIST" && /list\[[0-9]+\] = / { args[id] = args[id] " " $NF; next }
     END { print render(sink) }' go-cv.types >sink.actual
echo 'ptr8(ptr8(proc T_VOID(T_QUAD,T_QUAD)))' | diff - sink.actual
