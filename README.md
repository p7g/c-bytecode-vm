<!-- vim: set tw=79: -->

# c-bytecode-vm

[![TODOs](https://badgen.net/https/api.tickgit.com/badgen/github.com/p7g/c-bytecode-vm)](https://www.tickgit.com/browse?repo=github.com/p7g/c-bytecode-vm)

[Standard libary documentation](https://github.com/p7g/c-bytecode-vm/blob/master/docs/stdlib.md)

This is a small, weakly and dynamically typed, interpreted programming language
that doesn't really have a name. My goal is to see how close I can get to a
"real" programming language like Python while implementing everything on my own.

One of the goals I have with this language is to keep the built-in stuff
(implemented in C) to a minimum. You'll probably be able to tell throughout the
rest of this document.

To try any of these examples, clone the repository, run `make` (only MacOS and
Linux are supported right now), and start the repl by running `./cbcvm`. Use the
`--help` flag to see how to otherwise use the CLI.

Without further ado, here is an overview of the language:

## Comments

There are only line comments. They start with a `#` and end at the next newline.

## Primitive Types

Objects of a primitive type are immutable and passed around by value; if you
access a variable bound to a number, you get a copy of its value. These values
are stored directly on the (VM's) stack.

The primitive types are the following:

### Integer

A signed, 64-bit integer. The semantics are those in C, except that an error is
raised in the event of an overflow.

Examples:
```js
1234
0xabCdef
-6
```

### Double

A double-precision floating point value.

Examples:
```js
1.1
0.123
123.
-156.3
```

To be honest, supporting a trailing decimal is not intentional and I sort of
want to make that not work.

### Char

A single character. Stored as an unsigned 32-bit integer, though there is no
UTF-8 support at this time. This is on the list of things to do.

Examples:
```c
'a'
'\n'
'\''
```

This one seems uncommon for dynamic languages for whatever reason. Personally, I
like to have the guarantee that a char needs no allocations without having to
assume certain optimizations exist. This means that a dumb language like this
one won't suffer quite as much when processing strings.

### Bool

True or false.

Examples:
```js
true
false
```

### Null

A sentinel value used to represent the absence of a value.

Example: `null`

### String

A sequence of C `char` values represented as a "fat pointer"; a C char pointer
with an associated length. As previously mentioned, UTF-8 support is something
I plan to do.

Examples:
```js
"hello"
"\""
"this
spans
multiple
lines"
```

## Composite Data Types

There are two native types for aggregating values:

### Array

A fixed-length, heap-allocated, mutable series of values. Array elements are
accessed using brackets (`[]`).

Examples:
```js
[1, "hello", null]
[]
[1,]

some_array[3]
```

Accessing a index that is out of range will panic.

### Struct

Essentially an array with named indices. A struct has a "struct spec" associated
with it, but it has no "identity"; two structs of the same spec with identical
values are considered equal.

If a key is not specified when instantiating a struct spec, it is assigned null.

Struct fields are accessed using a `:`.

Examples:

```c
struct test { a, b }
test { a = 1 }

test:a
```

Accessing a struct field that does not exist results in a panic at run-time.

An anonymous struct can be declared and instantiated in one expression:
```c
println(struct { a = 123 });
```

### Function

Functions are closures. This means they can capture variables in the scope where
they are defined. As such I think it's (somewhat) reasonable to consider them a
composite data type.

A function is defined like so:
```js
function my_function(a, b, c) {
  return a + b * c;
}
```

A function declaration can also appear as an expression, in which case it will
not be associated with its name in the scope where it's defined. The name allows
it to call itself recursively.

Calling a function looks like this:
```js
my_function(1, 2, 3)
```

You can pass more arguments than the function declares, but not less.

A function can return a single value. A bare `return` statement will return the
value `null`.

## The Userdata Type

Userdata values are opaque and can store anything at the C level. These are only
useful for C extensions. Code in the hosted language cannot do anything with the
value other than move it around.

An example use-case is storing `FILE *` values, as seen in the `fs` standard
library module.

## Variable Declarations and Scoping Rules

### Syntax

A variable declaration uses the `let` keyword, like this:

```js
let a = 123;
let b;
let c = 3, d;
```

If no initializer is provided, the variable is initialized with `null`.

Structs and arrays can be destructured like so:

```js
let { a } = struct { a = 123, b = 234 };
let [b, c] = [1, 2, 3];
```

If a field is destructured that doesn't exist on the struct or if more elements
are destructured from an array than the array contains, the program will panic.

Any fields/elements which aren't matched on are ignored.

### Scoping Rules

Variables are scoped to their function, regardless of where the declaration is.
This is similar to how Python behaves, however there is no need for a `global`
or `nonlocal` statement equivalent, since declarations are explicit.

If a variable exists in the current function with a given name, references to
that name will refer to that variable. If not, the compiler will check for the
variable in the parent (lexical) scopes one by one. If it finds the variable in
a parent scope, that variable will be closed-over by the current function. If it
is not found, it is assumed to be a global variable.

Global variables only exist in the module in which they're defined.

Variables in a function's closure are references to a variable elsewhere. This
means that if multiple functions close over the same variable, they will see the
updates made by the others. Those changes would also be visible in the scope
where the variable was originally defined.

## Flow Control

A selection of the usual constructs are available. In all cases the braces
surrounding the block are required.

### If Statement

Works as you would expect:
```js
if (true) {
} else if (false) {
} else {
}
```

### While Loop

Works as you would expect:
```js
while (true) {}
```

### For Loop

The `for` loop is like a C for loop. The language has no native iteration
protocol, but see the "Iteration" section below for more on that.

```js
for (let a = 0; a < 10; a = a + 1) {}
```

Any of the initializer, condition, or whatever the third part is called can be
omitted.

## Errors and Error Handling

There is a pretty rudimentary error system that looks a bit like Lua's. It works
much like exceptions in JavaScript, but without any special syntax for it (just
special intrinsic functions).

To raise an error, the `panic` intrinsic function is used. Any value can be
passed to it as the error value.

The VM will unwind the call stack until the error state is recovered or it
reaches the top of the stack, at which point the program will exit. To recover
from an error state, the `pcall` intrinsic is used, like so:

```js
let [ok, value] = pcall(function () {
  this_might_fail();
  return "success";
});
```

## Module System

The module system works like Python's, but much more rudimentary.

A module is imported using an `import` statement:
```js
import test;
```

Upon encountering this statement, the compiler will search for a module with
that name. If a built-in module exists with that name, it's imported. If not,
the compiler will search for a file called (in this case) `test.rbcvm` in each
of the paths defined in the `CBCVM_PATH` environment variable. This variable
should be a colon-separated list of directories. Once it finds a module with
that name, any imports of that name will result in the same module being
imported.

Once a module is imported, its exports can be accessed by prefixing their name
with the module name and a dot, like `test.assert`. There is currently no way to
alias an imported module, nor is there a way to create individual bindings for
its exports while importing.

Note that the module name in an expression like `test.assert` is not resolved
like a variable; it is looked up in the list of imported modules directly. This
means that the following will not work:
```js
import test;
let t = test;
```

## Garbage Collection

c-bytecode-vm has a tracing (mark-and-sweep) garbage collector with some
(potentially-flawed) reference-counting extensions. Such a GC is immune to
cycles, at the expense of taking longer to collect.

The ref-counting mechanism is to allow C extensions to make sure a value is not
collected while they are using it. This works, but it could be unergonomic since
an incremented ref on an array does not apply to its contents, for example.

## Intrinsic Functions

Here are some of the intrinsic functions that likely won't be going anywhere, at
least for now:
- `print`: Write a string representation of some values to stdout without a
  trailing newline.
- `println`: Write a string representation of some values to stdout _with_ a
  trailing newline.
- `tostring`: Get a string representation of a given value.
- `type_of`: Get the type of a value as a string.
- `ord`: Convert a character to an integer.
- `chr`: Convert an integer to a character.
- `tofloat`: Convert an integer to a double. This should be renamed.
- `__upvalues`: Returns the values of the current function's closure as an array.
- `apply`: Passes an array of values to a function as individual arguments.
- `toint`: Converts a double to an integer, or does what `ord` does to a char.
- `__gc_collect`: Have the garbage collector run immediately. It has a scary
  name because it seems like a scary thing to do.
- `pcall`: Call a function, catching any errors raised during its execution and
  returning whether it succeeded.

## Standard Library

Standard library documentation lives in the source code. The generated markdown
file can be found
[here](https://github.com/p7g/c-bytecode-vm/blob/master/docs/stdlib.md).

Here are some idioms that have appeared in the standard library:

### Iteration

This language has no generators, nor any built-in iteration protocol. The way
this is done in the standard library (see the [iter] module) is using closures.

[iter]: https://github.com/p7g/c-bytecode-vm/blob/master/lib/iter.rbcvm

This is easiest to illustrate with an example:
```js
function range(to) {
  let i = 0;

  return function next() {
    if (i == to) {
      return null;
    }
    let val = i;
    i = i + 1;
    return val;
  };
}

test.assert(iter.collect(range(10)) == [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
```

Using constructs like these, iteration can be performed like in JavaScript:
```js
iter.foreach(range(10), function (n) {
  println(n);
});
```

Of course, the same issues in trying to exit the loop prematurely exist.

Modules that declare "types" that can be iterated over should export an `iter`
function that returns this kind of iterator.