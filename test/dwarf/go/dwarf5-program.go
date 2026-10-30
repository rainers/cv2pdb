package main

import "os"

//go:noinline
func Square(x int) int { return x * x }

func Twice(x int) int { return x + x }

type Value int

//go:noinline
func (v Value) Get() int { return int(v) + 1 }

type Getter interface{ Get() int }

var sink func(int) int

//go:noinline
func call(g Getter) int { return g.Get() }

func main() {
  sink = Twice
  os.Exit(Square(len(os.Args)) + Twice(len(os.Args)) + sink(1) + call(new(Value)) - 100)
}
