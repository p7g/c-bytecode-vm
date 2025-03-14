<!-- vim: set tw=79: -->

# c-bytecode-vm

[![TODOs](https://img.shields.io/github/search?query=repo%3Ap7g%2Fc-bytecode-vm%20AND%20TODO&label=TODOs)](https://github.com/search?type=code&q=repo%3Ap7g%2Fc-bytecode-vm+TODO)
[![FIXMEs](https://img.shields.io/github/search?query=repo%3Ap7g%2Fc-bytecode-vm%20AND%20FIXME&label=FIXMEs&color=red)](https://github.com/search?type=code&q=repo%3Ap7g%2Fc-bytecode-vm+FIXME)

[Standard libary documentation](https://p7g.github.io/c-bytecode-vm/docs/stdlib)
|
[Performance tracking](https://p7g.github.io/c-bytecode-vm/dev/bench)

This is a small, weakly and dynamically typed, interpreted programming language
that doesn't really have a name. My goal is to see how close I can get to a
"real" programming language like Python while implementing everything on my own.

Another goal I have with this language is to keep the built-in stuff
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

A signed 64-bit integer. Hexadecimal literals are supported (with a lowercase X).

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
123.0
-156.3
```

### Char

A single Unicode codepoint.

Examples:
```c
'a'
'\n'
'\''
```

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

A UTF-8-encoded string. Can be manipulated using functions in the `string`
module.

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

There are three native types for aggregating values:

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

Accessing an index that is out of range will throw.

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

test.a
```

Accessing a struct field that does not exist results in a throw at run-time.

An anonymous struct can be declared and instantiated in one expression:
```c
println(struct { a = 123 });
```

Note that each anonymous struct declaration has its own struct spec. This means
that two anonymous structs declared in different places will never be equal.
For example:

```c
function makestruct() { return struct {}; }

println(makestruct() == makestruct());  # true
println(makestruct() == struct {});  # false
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
are destructured from an array than the array contains, the program will throw.

Any fields/elements which aren't matched on are ignored.

### Scoping Rules

Variables are lexically scoped.

If a variable exists in the current scope with a given name, references to that
name will refer to that variable. If not, the compiler will check for the
variable in the parent function scopes one by one. If it finds the variable in
a parent scope, that variable will be closed-over by the current function. If
it is not found, it is assumed to be a global variable.

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
for (let a = 0; a < 10; a++) {}
```

Any of the initializer, condition, or whatever the third part is called can be
omitted.

## Errors and Error Handling

There is a pretty rudimentary exception system. It works much like exceptions
in JavaScript, using try/catch and throw. You can throw any value, and
a try/catch block will catch all errors regardless of type.

In the event of an error, the VM will unwind the call stack until the error
state is recovered or it reaches the top of the stack, at which point the
program will exit. Here's an example:

```js
try {
  this_might_fail();
  return "success";
} catch (e) {
  return "failed";
}
```

## Module System

A module is imported using an `import` statement:

```js
import test;
```

Upon encountering this statement, the compiler will search for a module with
that name. If a built-in module exists with that name, it's imported. If not,
the compiler will search for a file called (in this case) `test.cb` in each
of the paths defined in the `CBCVM_PATH` environment variable. This variable
should be a colon-separated list of directories. Once it finds a module with
that name, any imports of that name will result in the same module being
imported.

Once a module is imported, its exports can be accessed by prefixing their name
with the module name and a dot, like `test::assert`. There is currently no way
to alias an imported module, nor is there a way to create individual bindings
for its exports while importing.

Note that the module name in an expression like `test::assert` is not resolved
like a variable; it is looked up in the list of imported modules directly. This
means that the following will not work:

```js
import test;
let t = test;
```

## Garbage Collection

c-bytecode-vm has a tracing (mark-and-sweep) garbage collector.

To allow C extensions to safely hold onto values without them being collected,
use `cb_gc_hold` to add it to a GC root, and `cb_gc_release` to remove it from
the root. The next time the GC runs the value may be eligible for collection.

## Intrinsic Functions

Here are some of the intrinsic functions that likely won't be going anywhere, at
least for now:
- `print`: Write a string representation of some values to stdout without a
  trailing newline.
- `println`: Write a string representation of some values to stdout _with_ a
  trailing newline.
- `tostring`: Get a string representation of a given value.
- `typeof`: Get the type of a value as a string.
- `ord`: Convert a character to an integer.
- `chr`: Convert an integer to a character.
- `tofloat`: Convert an integer to a double. This should be renamed.
- `__upvalues`: Returns the values of the current function's closure as an array.
- `apply`: Passes an array of values to a function as individual arguments.
- `toint`: Converts a double to an integer, or does what `ord` does to a char.
- `__gc_collect`: Have the garbage collector run immediately. It has a scary
  name because it seems like a scary thing to do.
- `__dis`: Print the disassembly of the given function directly to stdout.

## Standard Library

Standard library documentation lives in the source code. The generated markdown
file can be found
[here](https://github.com/p7g/c-bytecode-vm/blob/master/docs/stdlib.md).

Here are some patterns that have appeared in the standard library:

### Iteration

Using [traits], the [iter] module defines an iteration protocol. This protocol
involves 2 traits: `Iterable` and `Iterator`.

[traits]: /blob/main/lib/trait.cb
[iter]: /blob/main/lib/iter.cb

The `Iterable` trait makes an object iterable (as the name suggests). It has a
single method called `iter(self)` that must return an `Iterator`. Objects of any
type that implements this trait can be passed as the iterable argument to any
function in the `iter` module.

An `Iterator` object is a stateful iterator with a `next(self)` parameter, which
must return the next item in the sequence.

Here's an example of both:

```c++
struct Range { start, stop, step }
struct RangeIterator { range, i }

trait::impl(iter::Iterable, Range, struct {
  function iter(self) {
    return RangeIterator { range = self }
  }
});

trait::impl(iter::Iterator, RangeIterator, struct {
  function next(self) {
    if (self.i == null) {
      self.i = self.range.start;
    }

    if (self.i >= self.range.end) {
      return iter::STOP;
    }

    let { i } = self;
    self.i += self.range.step;
    return i;
  }
});

function range(start, stop=null, step=null) {
  if (stop == null) {
    return Range { start = 0, stop = start, step = 1 };
  } else {
    if (step == null) {
      step = 1;
    }
    return Range { start = start, stop = stop, step = step };
  }
}
```

It's common to implement `iter::Iterable` for any type that also implements
`iter::Iterator`; that way you can use existing iterators as arguments for
`iter` functions.

Once you have an object that implements the iteration protocol, you can use it
like this:
```c++
iter::foreach(range(10), function (n) {
  println(n);
});
```

To "break" out of `foreach` just return `iter::STOP` (the same sentinel used to
signal when an iterator is exhausted).

Modules that declare "types" that can be iterated over should export an `iter`
function that returns this kind of iterator.