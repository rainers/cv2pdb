package main

import (
  "debug/dwarf"
  "debug/pe"
  "fmt"
  "os"
)

func next(r *dwarf.Reader) *dwarf.Entry {
  e, err := r.Next()
  if err != nil {
    panic(err)
  }
  return e
}

func declares(r *dwarf.Reader, block *dwarf.Entry, name string) bool {
  if !block.Children {
    return false
  }
  found := false
  for e := next(r); e.Tag != 0; e = next(r) {
    found = found || (e.Tag == dwarf.TagVariable && e.Val(dwarf.AttrName) == name)
    r.SkipChildren()
  }
  return found
}

// Prints the start and end address of each inlined call that lies in a gap of a lexical block.
// The arguments name the PE file, the function, and a variable that the lexical block declares.
// Only inlined calls and lexical blocks that are direct children of the function count. A call
// lies in a gap when one range of the block ends at or before it and another starts at or after it.
func main() {
  f, err := pe.Open(os.Args[1])
  if err != nil {
    panic(err)
  }
  d, err := f.DWARF()
  if err != nil {
    panic(err)
  }
  r := d.Reader()
  for {
    e := next(r)
    if e == nil {
      return
    }
    if e.Tag == dwarf.TagSubprogram && e.Val(dwarf.AttrName) == os.Args[2] {
      break
    }
  }
  var blocks, calls [][][2]uint64
  for e := next(r); e.Tag != 0; e = next(r) {
    ranges, err := d.Ranges(e)
    if err != nil {
      panic(err)
    }
    if e.Tag == dwarf.TagLexDwarfBlock && declares(r, e, os.Args[3]) {
      blocks = append(blocks, ranges)
    } else if e.Tag == dwarf.TagInlinedSubroutine && len(ranges) != 0 {
      calls = append(calls, ranges)
    }
    r.SkipChildren()
  }
  for _, call := range calls {
    lo, hi := call[0][0], call[0][1]
    for _, r := range call {
      lo, hi = min(lo, r[0]), max(hi, r[1])
    }
    for _, block := range blocks {
      before, after := false, false
      for _, r := range block {
        before = before || r[1] <= lo
        after = after || r[0] >= hi
      }
      if before && after {
        fmt.Printf("%x-%x\n", lo, hi)
        break
      }
    }
  }
}
