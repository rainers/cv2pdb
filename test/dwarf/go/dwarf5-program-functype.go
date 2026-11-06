package main

import (
  "debug/dwarf"
  "debug/pe"
  "fmt"
  "os"
  "strings"
)

// Prints the DWARF tags of the type of the variable named by the second argument in the PE file
// named by the first argument, following typedefs and pointers. A subroutine type is printed with
// its byte size, as in SubroutineType:8. Nothing is printed if the PE file has no variable with the
// name given by the second argument.
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
  var off dwarf.Offset
  for off == 0 {
    e, err := r.Next()
    if err != nil {
      panic(err)
    }
    if e == nil {
      return
    }
    if e.Tag == dwarf.TagVariable && e.Val(dwarf.AttrName) == os.Args[2] {
      off, _ = e.Val(dwarf.AttrType).(dwarf.Offset)
    }
  }
  var tags []string
  for off != 0 {
    r.Seek(off)
    e, err := r.Next()
    if err != nil {
      panic(err)
    }
    tag := e.Tag.String()
    if size, ok := e.Val(dwarf.AttrByteSize).(int64); ok && e.Tag == dwarf.TagSubroutineType {
      tag = fmt.Sprintf("%s:%d", tag, size)
    }
    tags = append(tags, tag)
    off = 0
    if e.Tag == dwarf.TagTypedef || e.Tag == dwarf.TagPointerType {
      off, _ = e.Val(dwarf.AttrType).(dwarf.Offset)
    }
  }
  fmt.Println(strings.Join(tags, " "))
}
