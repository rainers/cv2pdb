set -ex
cp "$TESTDIR/dwarf5-program.go" main.go
mkdir -p emptyloc
cp "$TESTDIR/dwarf5-program-emptyloc.go" emptyloc/main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o go-dwarf.exe main.go
"$CV2PDB" go-dwarf.exe go-cv.exe
"$CVDUMP" -s go-cv.pdb >go-cv.dump

# Variables with an empty DWARF location get no S_REGREL32 record.
go run emptyloc/main.go go-dwarf.exe >emptyloc.txt
test -s emptyloc.txt || { echo "test precondition lost: no variable with an empty location"; exit 1; }
awk 'NR == FNR { want[$1 " " $2] = 1; next }
     $2 == "S_GPROC32:" { proc = $NF }
     $2 == "S_REGREL32:" && (proc " " $NF) in want { print; bad++ }
     END { exit bad > 0 }' emptyloc.txt go-cv.dump
