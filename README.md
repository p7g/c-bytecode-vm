<!-- vim: set tw=79: -->

# c-bytecode-vm

[![TODOs](https://badgen.net/https/api.tickgit.com/badgen/github.com/p7g/c-bytecode-vm)](https://www.tickgit.com/browse?repo=github.com/p7g/c-bytecode-vm)

This is a C translation of [rust-bytecode-vm]. The goal here is to see what (if
any) performance improvement there is to be had with a C implementation.

[rust-bytecode-vm]: https://github.com/p7g/rust-bytecode-vm

Some factors that I think could play a part:

- More obvious allocations
- Garbage collection
- Computed gotos for main interpreter loop

At the same time, I would like to keep this implementation as simple as
possible. There are probably many areas of rust-bytecode-vm that are more
complex than they need to be. On the other hand, a C implementation with no
dependencies will need its own implementations of dynamic arrays and hashmaps,
which will likely more than make up for complexity savings elsewhere.

## Plan

The evaluation of a program will be formally broken down into the following
parts:

0. Lexing, parsing, and Code generation
1. Bytecode interpretation
   a. Virtual machine
   b. Language runtime
2. Garbage collection

### Lexing, Parsing, and Code Generation

The lexer and parser is written by hand. This allows us to skip some
dependencies, and makes it simpler to provide useful errors to the user at
parse-time.

The parser is a simple recursive-descent parser, like the one in
rust-bytecode-vm. This has served its purpose well there; parsing doesn't take
long enough to be worth worrying about.

For simplicity's sake, a parse or compile error will result in the (almost)
immediate termination of the program. During parsing, any identifiers and
string literals will be interned in the agent, which is just a global instance.
Interned strings are stored globally so the interpreter will have access to
them as well.

The set of op codes is nearly the same as those defined in rust-bytecode-vm.
Hopefully this will provide a more apples-to-apples comparison.

Code is generated in one pass. This is a departure from rust-bytecode-vm,
which parses the source into an AST, and later traverses it to compile the
bytecode. This likely won't make a huge performance difference for non-trivial
applications. An interesting implication of this is that a for loop requires
more jumps than in rust-bytecode-vm.

A requirement is to support the same programs as rust-bytecode-vm.

### Bytecode Interpretation

The interpretation of bytecode is the job of the virtual machine, which
delegates business logic to the language runtime. As an example, upon reaching
an "add" opcode, the virtual machine would retrieve two values from the stack,
call into the runtime to sum them or report an error, and then push the result
back onto the stack.

I haven't been very good at keeping to this, but it is a goal to pull that
logic out of the interpreter loop where possible.

#### Virtual Machine

The architecture of the virtual machine is similar to that of rust-bytecode-vm.
As opposed to a single massive switch statement, computed gotos are used.
Thanks to this, there are many branching points within the interpreter loop
rather than one, which should result in better usage of the CPU's branch
predictor (or so they say).

Initially, I planned to try to support compilers without GNU C extensions, but
that didn't happen. Maybe eventually that will be a goal, but for now GNU
extensions are used all over the place (turns out they're pretty nice).

For profiling and debugging purposes, each opcode's handler _should_ be its own
function. Ideally the compiler will inline these functions where necessary.
This is entirely not the case right now.

#### Runtime

The runtime will mainly consist of a set of functions that operate on language
values. This includes comparison, allocation and deallocation, and operations
like addition and array indexing.

Values captured by a closure will use upvalues, as in rust-bytecode-vm. This
means that until a value has escaped its scope, it remains on the stack. The
upvalue at this point contains an integer index onto the stack, where it exists
alongside adjacent locals. Once the value escapes, the value is retrieved from
the stack and stored in the upvalue structure. These states are called "open"
and "closed" respectively. When a function returns, any upvalues pointing to
locals in its scope are closed before the stack is truncated to return to the
caller.

#### Garbage Collection

This interpreter will use a tracing garbage collector. This type of collector
marks all accessible objects, then collects any that have not been marked.

This means we must keep track of all objects that have been allocated. To do
so, each value will have a header which contains garbage collection
information. A field of this header will be a pointer to the next object,
forming a linked list. This list will be traversed to collect garbage.

To mark any accessible objects, we need to define the "roots". In this case,
the roots will be as follows:

- Values on the stack, and all values accessible from those values
- Upvalues stored in the interpreter state, and any value accessible from there
- Each module's global scope, as well as the global scope for any files that
  are not modules (this is new with c-bytecode-vm)

A collection can happen on any allocation. As such, we need a way to prevent
objects being used from C code (i.e. inaccessible according to the garbage
collector) from being collected.

CPython uses a refcounting GC to avoid this issue. A C extension must increment
and decrement a refcount on each object. Only when this value reaches 0 is it
collectible. This results in more cluttered extension code. The MRI Ruby
interpreter, on the other hand, traces the C stack to find values in use.

To keep things simple, the GC header on every object will also contain a ref
count. When C code uses on object, it will increment this count, which will
prevent an object from being collected, even if it was not marked.
