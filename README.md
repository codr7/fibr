## fibr

### intro
fibr aims to implement a minimal, reasonably fast, practically useful and hackable interpreter in 1kloc of C.

### setup
fibr requires CMake and a C compiler to build, rlwrap is highly recommended for running the REPL.

```
$ cd fibr
$ mkdir build
$ cd build
$ cmake ..
$ make
$ rlwrap ./fibr
fibr 4

+ 35 7;
[42]
```

### repl
The REPL evaluates read forms up to the next semicolon.

```
foo;bar
Error in repl, line 0 column 0: Unknown id: foo

;
Error in repl, line 0 column 4: Unknown id: bar
```

### the stack
`d+` may be used to drop values from the stack.

```
1 2 3 4 5 dd;
[1 2 3]
```

### branches
`if` may be used to branch on any condition.

```
if 42 T F;
[T]

if 0 T F;
[T F]
```