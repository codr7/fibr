## fibr

### intro
**fibr** aims to implement a minimal, reasonably fast, practically useful and hackable interpreter in C that's advanced enough to calculate the fibonacci sequence tail-/recursively and easy to extend.

### setup
**fibr** requires `make` and a C-compiler to build, rlwrap is highly recommended for running the REPL.

```
$ git clone https://github.com/codr7/fibr.git
$ cd fibr
$ make
$ rlwrap ./fibr
fibr 6

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

### placeholders
`_` may be used as a placeholder wherever the syntax requires a value. It has no runtime effects, which means that whatever is on top of the stack will be used instead.

```
35 + _ 7;
[42]
```