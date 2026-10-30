package main

import (
  "debug/dwarf"
  "debug/pe"
  "fmt"
  "os"
  "strings"
)

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
  fn := ""
  for {
    e, err := r.Next()
    if err != nil {
      panic(err)
    }
    if e == nil {
      break
    }
    name, _ := e.Val(dwarf.AttrName).(string)
    switch e.Tag {
    case dwarf.TagSubprogram:
      fn = name
    case dwarf.TagFormalParameter, dwarf.TagVariable:
      loc := e.AttrField(dwarf.AttrLocation)
      if strings.HasPrefix(fn, "main.") && loc != nil && loc.Class == dwarf.ClassExprLoc && len(loc.Val.([]byte)) == 0 {
        fmt.Println(strings.ReplaceAll(fn, ".", "@"), name)
      }
    }
  }
}
