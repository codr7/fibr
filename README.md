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
fibr 1

42;
[42]
```

### repl
The repl evaluates read forms up to the next semicolon.

```
foo;bar
Error in repl, line 0 column 0: Unknown id: foo

;
Error in repl, line 0 column 4: Unknown id: bar
```