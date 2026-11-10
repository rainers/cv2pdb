package main

import "os"

var sink int

//go:noinline
func use(count int) { sink += count }

func twice(value int) int {
  return value*2 + len(os.Args)
}

//go:noinline
func Walk(xs []int) int {
  total := 0
  for step := 0; step < len(xs); step++ {
    use(step)
    total += twice(xs[step])
    use(step)
  }
  return total
}

func main() {
  os.Exit(Walk([]int{len(os.Args), 2, 3}))
}
