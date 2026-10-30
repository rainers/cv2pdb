package main

import "os"

var sink []string

//go:noinline
func record(n int) { sink = append(sink, os.Args[n%len(os.Args)]) }

//go:noinline
func Count(s string) int {
  n := 0
  for i := 0; i < len(s); i++ {
    if s[i] == '/' {
      n++
    }
  }
  record(n)
  return n + len(s) + int(s[len(s)-1])
}

func main() {
  os.Exit(Count(os.Args[0]) - 1000)
}
