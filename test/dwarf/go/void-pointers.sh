set -ex
cp "$TESTDIR/dwarf5-program.go" main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o go-dwarf.exe main.go
"$CV2PDB" go-dwarf.exe go-cv.exe
"$CVDUMP" -g go-cv.pdb >go-cv.globals
"$CVDUMP" -t go-cv.pdb >go-cv.types

# The global record of an unsafe.Pointer variable has a pointer to void as its type.
opaque=$(awk '$1 == "S_GDATA32:" && $NF == "main@opaque" { t = $4; sub(/,$/, "", t); print tolower(t) }' go-cv.globals)
test -n "$opaque" || { echo "test precondition lost: no global record for main.opaque"; exit 1; }
awk -v t="$opaque" '$2 == ":" && $3 == "Length" { inrec = $1 == t; next }
     inrec && $1 == "Element" { found = $4 == "T_VOID(0003)" }
     END { exit !found }' go-cv.types
