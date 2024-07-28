# cbcvm Documentation

Index:
- [docs](#docs-module)
- [iter](#iter-module)
- [array](#array-module)
- [arraylist](#arraylist-module)
- [assoclist](#assoclist-module)
- [bytes](#bytes-module)
- [char](#char-module)
- [math](#math-module)
- [fn](#fn-module)
- [op](#op-module)
- [string](#string-module)
- [test](#test-module)
- [base64](#base64-module)
- [box](#box-module)
- [fs](#fs-module)
- [hash](#hash-module)
- [hashmap](#hashmap-module)
- [json](#json-module)
- [list](#list-module)
- [obj](#obj-module)
- [path](#path-module)
- [re](#re-module)


## docs module

Tools to document module exports.

### function `print_generator(inner_generator)`

Create a generator that prints the result of `inner_generator` to stdout.

### function `single_page_markdown()`

Create a documentation generator that results in a single string of markdown.

### function `single_page_html()`

Create a documentation generator that results in a single string of HTML.

### function `generate(generator)`

Generate documentation using `generator` for all items already added to the registry.

### struct `generator`

The interface for a documentation generator.

### function `module`

Create an object to document members of a module.

## iter module

Lazy iterators.

### function `intersperse(it, val)`

Yield `val` in between each element of `it`.

### function `repeat(val, n)`

Create an iterator that repeats `val` `n` times.

### function `min(it)`

Find the smallest item in the iterator (using `<`).

### function `max(it)`

Find the largest item in the iterator (using `>`).

### function `fold(it, init, reducer)`

Reduce the iterator, using `init` as the initial value of the accumulator.

### function `fold1(it, reducer)`

Reduce the iterator, using the first value as the initial value of the accumulator.

### function `flat(it)`

Flattens an iterator of iterators.

### function `zip(a, b)`

Create an iterator that joins `a` and `b` pair-wise.

### function `take_while(it, pred)`

Create an iterator that yields the values of `it` until `pred` returns false.

### function `drop(it, n)`

Create an iterator that ignores the first `n` elements of `it`.

Note that calling this function immediately evaluates and drops the first `n`
values of `it`.

### function `take(it, n)`

Create an iterator that evaluates the first `n` elements of `it`.

Note that evaluating this iterator partially evalutaes `it`.

### function `map(it, fn)`

Return a new iterator that yields the values of `it` applied to `fn`.

### function `collect(it)`

Evaluate an iterator, returning an array of the values it yields.

### function `foreach(it, fn)`

Call the function `fn` for every element of `it`. This evaluates the iterator.

### function `enumerate(it)`

Create an iterator that yields pairs of index and value from `it`.

### function `range(to)`

Create an iterator that counts from 0 to `to`.

### function `count(n)`

Create an iterator that counts up from `n` indefinitely.

### struct `collector`

An struct defining the required functions to convert an iterator into an arbitrary collection.

### object `STOP`

A sentinel value indicating that an iterator has been exhausted.

## array module

Functions for working with arrays.

### function `slice(array, start, end)`

Returns a slice of `array` starting at index `start` and ending at index `end`.

### function `reversed(array)`

Create a new array with the elements of `array` reversed.

### function `reverse(array)`

Reverse `array` in place.

### function `foldl(array, init, reducer)`

Reduce `array` from the left, using the first value of `array` as the initial
value for the accumulator.

### function `foldl(array, init, reducer)`

Reduce `array` from the left, using `init` as the initial value for the
accumulator.

### function `map(array, func)`

Create a new array where each element is the result of calling `func` on the
corresponding element of `array`.

### function `contains(array, thing)`

Returns true if any element of `array` is equal to `thing`.

### function `foreach(array, func)`

Call `func` with the element, index, and array for each element of `array`.

### function `find(array, predicate)`

Find the element for which `predicate` returns true. If there is none, returns null.

### function `find_index(array, predicate)`

Find the index of the element for which `predicate` returns true. If there is none, returns -1.

### object `collector`

A collector for converting an iterator into an array.

### function `iter(array)`

Create an iterator from array `array`.

### function `length(array)`

Get the length of array `array`.

### function `new(len)`

Create a new array of length `len`.

## arraylist module

A growable array.

### function `contains(list, thing)`

Check if any element in `list` is equal to `thing`.

### function `some(list, func)`

Check if any element in `list` satisfies the predicate `func`.

### function `find(list, func)`

Get the first element that satisfies the predicate `func`, or null if none exists.

### function `map(list, func)`

Create a new arraylist where each element is the result of calling `func` on
the corresponding element of `list`.

### function `find_index(list, func)`

Get the index of the first element that satisfies the predicate `func`, or -1
if none exists.

### function `foreach(list, func)`

Call `func` for each element of `list`.

### function `pop(list)`

Remove and return the last element of `list`.

### object `collector`

A collector for converting an iterator into an arraylist.

### function `push(list, value)`

Push `value` on to the end of `list`, growing it if necessary.

### function `to_array(list)`

Copy the elements of `list` into a fixed-length array.

### function `delete(list, idx)`

Remove the element at index `idx` from `list`. This involves shuffling all
elements after `idx` leftward, so it's not very efficient.

### function `set(list, idx, value)`

Set the element of `list` at index `idx` to `value`.

### function `get(list, idx)`

Get the element of `list` at index `idx`.

### function `capacity(list)`

Get the maximum number of items `list` can hold.

### function `length(list)`

Get the number of items in `list`.

### function `new()`

Create an empty arraylist with default initial capacity.

### function `with_capacity(cap)`

Create an empty arraylist with the given initial capacity.

### function `iter(list)`

Create an iterator over arraylist `list`.

### function `from_array(len, array)`

Create a new arraylist from `array`. The `len` argument should be the length
of `array`.

## assoclist module

A map data structure backed by an arraylist of key-value pairs.

### function `entries(list)`

Get all entries from `list`.

### function `is_empty(list)`

Check if `list` has no elements.

### function `has(list, key)`

Check if an entry for `key` exists in `list`.

### function `delete(list, key)`

Delete the first entry for `key` in `list`.

### function `get(list, key)`

Get the first value of `key` in `list`.

### function `set(list, key, value)`

Set the value of `key` in `list` to `value`.

### object `collector`

A collector for converting an iterator of
entries into an assoclist.

### function `iter(list)`

Create an iterator over the entries of `list`.

### function `length`

Check the number of items in the list.

### function `new`

Create an empty assoclist.

## bytes module

A compact byte array type.

### function `iter(bytes)`

Create an iterator over `bytes`.

### object `collector`

A collector for converting an iterator into byte array.

### function `length(bytes)`

Get the length of `bytes`.

### function `set(bytes, i, val)`

Set the `i`th value in `bytes` to `val`.

### function `get(bytes, i)`

Get the `i`th element from `bytes`.

### function `copy(from, to, n)`

Copy `n` bytes from `from` to `to`.

### function `new(size)`

Create a new byte array of size `size`.

## char module

Functions for working with characters.

### function `to_digit(c)`

Get the integer value of the digit character `c`.

### function `to_lowercase(c)`

Return the lowercase version of `c`.

### function `to_uppercase(c)`

Return the uppercase version of `c`.

### function `is_whitespace(c)`

Check if `c` is an ASCII whitespace character.

### function `is_uppercase(c)`

Return true if `c` is lowercase.

### function `is_uppercase(c)`

Return true if `c` is uppercase.

### function `is_alpha(c)`

Return true if `c` is an ASCII letter.

### function `is_digit(c)`

Return true if `c` is an ASCII digit.

## math module

Math functions.

### function `floor(n)`

Round `n` down to the previous integer value.

### function `ceil(n)`

Round `n` up to the next integer value.

### function `shr(a, b)`

Shift `a` to the right by `b` bits.

### function `shl(a, b)`

Shift `a` to the left by `b` bits.

### function `sqrt(n)`

Calculate the square root of `n`.

### function `abs(n)`

Calculate the absolute value of `n`.

## fn module

Functional programming utilities.

### function `chain(functions)`

The same as `compose`, except the values are piped left-to-right.

### function `compose(functions)`

Pipe the function return values from right-to-left.

For example, `compose([a, b, c])("test")` is equivalent to `a(b(c("test")))`.

### function `partial(fn, args)`

Honestly not really sure what this does lol

### function `curry(fn, a)`

Return a `fn` partially-applied with `a`.

### function `flip(f)`

Return a function `g(a, b)` which calls `f(b, a)`.

### function `identity(x)`

Return `x`.

### function `apply(func, args)`

Call `func`, passing as arguments the elements of the array `args`.

## op module

Functions that wrap operators.

### function `bxor(a, b)`

Performs `a ^ b`.

### function `bor(a, b)`

Performs `a | b`.

### function `band(a, b)`

Performs `a & b`.

### function `mod(a, b)`

Performs `a % b`.

### function `div(a, b)`

Performs `a / b`.

### function `mul(a, b)`

Performs `a * b`.

### function `neg(a)`

Performs `-a`.

### function `sub(a, b)`

Performs `a - b`.

### function `add(a, b)`

Performs `a + b`.

### function `gte(a, b)`

Performs `a >= b`.

### function `gt(a, b)`

Performs `a > b`.

### function `lte(a, b)`

Performs `a <= b`.

### function `lt(a, b)`

Performs `a < b`.

### function `ne(a, b)`

Performs `a != b`.

### function `eq(a, b)`

Performs `a == b`.

## string module

Functions for working with strings.

### function `join(strings, sep)`

Join all `strings` together with `sep` in between each.

### function `split(string, on)`

Break a string into an array of parts, where each part is separated by `on`.

### function `strip(string, lpred=char.is_whitespace, rpred=null)`

Remove characters from both ends of the string while predicates remain true.
If only `lpred` is passed it's used for the start and end of the string.
If `rpred` is also passed, it's used for the end of the string.

### function `rstrip(string, pred=char.is_whitespace)`

Remove characters from the end of the string while `pred` remains true.

### function `lstrip(string, pred=char.is_whitespace)`

Remove characters from the start of the string while `pred` remains true.

### function `contains(string, c)`

Check if `string` contains the character `c`.

### function `rindex(string, c)`

Find the last index where `c` is found in `string`.

### function `index(string, c)`

Find the first index where `c` is found in `string`.

### function `endswith(string, suffix)`

Check if `suffix` is a suffix of `string`.

### function `startswith(string, prefix)`

Check if `prefix` is a prefix of `string`.

### function `slice(string, start, end)`

Get a substring of `string` starting from `start` and ending at `end`.

### function `parse_float(str)`

Parse `str` as a floating point number.

### function `parse_integer(str, base)`

Parse `str` as an integer of base `base`. Currently only works for base 10.

### object `collector`

A collector to convert an iterator of characters
or strings into a string.

### function `iter(str)`

Create an iterator over the characters in `str`.

### function `from_bytes(bytes)`

Convert a byte array to a string.

### function `char_at(string, idx)`

Get the `idx`th character of `string`.

### function `length(string)`

Get the length of `string`.

### function `concat(a, ...)`

Concatenate all strings passed as arguments with no separator.

### function `bytes(string)`

Get the bytes of `string` as an array.

### function `from_chars(chars)`

Create a string from the array of characters `chars`.

### function `chars(string)`

Get the characters of `string` as an array.

## test module

Testing utilities.

### function `assert(cond)`

Panics if `cond` is not truthy.

### struct `AssertionError`

Error raised when an assertion fails.

## base64 module

Base64 encoding and decoding functions.

### function `encode(payload, out=null)`

Encode an array of bytes into a base64 string. A single `out` buffer can be reused between calls to `encode` for better performance.

### function `decode(data)`

Decode a base64 string into an array of ints.

### function `decode_size(payload)`

Calculate the number of bytes that will result from decoding `payload`.

### function `encode_size(payload)`

Calculate the size of the base64 encoding of the byte array `payload`.

## box module

A simple wrapper type to emulate references.

### function `get(self)`

Get the value that's inside the box.

### function `set(self, value)`

Change the value in the box to `value`.

### function `new(value)`

Make a new box containing `value`.

## fs module

Filesystem operations.

### Note ``

Lower-level functions and constants are exported from `_fs`.

These are not documented, and will eventually move here.

### function `issock(path)`

Check if `path` points to a socket.

### function `islink(path)`

Check if `path` points to a link.

### function `isdir(path)`

Check if `path` points to a directory.

### function `isfile(path)`

Check if `path` points to a file.

### function `directory_entries(path)`

Get an iterator over the entries of a directory, excluding '.' and '..'.

### function `read_file(name)`

Read the file called `fname` to completion.

This should also work with fifos and stuff.

## hash module

Hashing functions.

### function `string(s)`

A hash function for strings.

### function `char(c)`

A hashing function for character values.

### function `int(n)`

A hashing function for integer values.

### function `fnv1a(bytes)`

Applies the 32bit FNV-1a hashing algorithm to a byte array.

## hashmap module

An implementation of a hash map.

### function `extend(map, entries)`

Add each key-value pair in the `entries` iterator to `map`.

### function `from_entries(entries)`

Call `from_entries_with_hash_function` with the default hash function and `entries`.

### function `from_entries_with_hash_function(hash, entries)`

Create a new hashmap with `hash` as its hash function, and populate it from
the iterator of key-value pairs `entries`.

### function `make(init)`

Call `make_with_hash_function` with the default hash function and `init`.

### function `make_with_hash_function(hash, init)`

Make a new hashmap with `hash` as its hash functions, and call `init` to
initialize the map.

The `init` function receives a function that can be called with a key and value
to set them on the new map.

### function `values(map)`

Get an iterator over the values in `map`.

### function `keys(map)`

Get an iterator over the keys in `map`.

### function `size(map)`

Count the number of values in `map`.

### function `iter(map)`

Get an iterator over the key-value pairs in `map`.

### function `entries(map)`

Get an iterator over the key-value pairs in `map`.

### function `has(map, key)`

Check if `map` has a value for `key`.

### function `delete(map, key)`

Delete `key` from `map`.

### function `set(map, key, value)`

Set the value associated with `key` in `map` to `value`.

### function `get(map, key)`

Get the value associated with `key` from `map`.

### function `clear(map)`

Delete all values from the map.

### function `with_hash_function(func)`

Create a new hash map that uses `func` as its hash function.

### function `set_hash_function(map, func)`

Set the hash function used by `map` to `func`.

### function `new()`

Create a hashmap in the default configuration.

### function `with_capacity(n)`

Create a new hashmap with `n` buckets.

### function `collector(hash_fn=...)`

Create a collector for converting
an iterator of entries into a hashmap.

## json module

A partial implementation of a JSON parser.

### function `parse(input)`

Parse the string `input` as JSON.

### struct `InvalidJsonError`

Error raised when the input string is not able to be parsed.

### struct `UnexpectedTokenError`

Error raised when an unexpected token is encountered.

## list module

A linked list implementation.

### function `foldl(list, init, reducer)`

Reduce the list from the left.

### function `to_array(list)`

Create a new array with the elements of `list`.

### function `map(list, func)`

Create a new linked list where every element is calculated by calling `func` on the old element.

### object `collector`

A collector to convert an iterator into a list.

### function `reverse(list)`

Make a new linked list that is the reverse of `list`.

### function `find(list, func)`

Return the first element of `list` that satisfies the predicate `func`.

If none is found, this function returns `null`.

### function `foreach(list, func)`

Call `func` for every element of `list`.

### function `append(list, value)`

Return a new list with `value` added to the end.

Like `length`, this function is O(n).

### function `prepend(list, value)`

Return a new list with `value` added to the front.

This function is O(1) (i.e. takes the same time regardless of the length of the list).

### function `length(list)`

Get the length of the list.

Note that this is O(n) (i.e. requires traversing the entire list).

### function `iter(list)`

Create an iterator over `list`.

### function `new()`

Create a new, empty linked list.

## obj module

A simple object system.

### function `setattr(obj, name)`

Set an attribute on the internal dictionary of `obj`.

### function `getattr(obj, name)`

Get an attribute from the internal dictionary of `obj`.

### function `super(obj, msg, args)`

Call the method `msg` on the superclass object of `obj`.

### function `send(obj, msg, args)`

Call the method `msg` on `obj`.

### function `_send(cls, obj, msg, args)`

Manually call the method `msg` on class `cls`, using `obj` as the instance.

Note that there is no guarantee that the method called is compatible with `obj`.

### function `getsuper(cls)`

Get the superclass of the class `cls`.

### function `getclass(obj)`

Get the class of `obj`.

### function `new(class, args)`

Create an instance of a class.

### function `class(super, methods)`

Define a class that extends `super`.

The `methods` value should be a hashmap of functions.


### struct `NoSuchMethodError`

Error raised when an unrecognized message is sent to an object.

## path module

Utilities for manipulating filesystem paths.

### function `ext(path)`

Get the extension of `path`, without the '.'.

### function `basename(path)`

Get the portion of `path` after the last '/'.

If there is no '/' in `path`, the same string is returned.

### function `dirname(path)`

Return the portion of `path` that is before the last '/'.

If there is no '/' in `path`, '.' is returned.

### function `join(a, b)`

Join the paths `a` and `b` with a '/'.

## re module

Primitive regular expressions.

### function `match(nfa, text)`

Match some text against an NFA generated by `compile`.

### function `compile(exp)`

Compile a regular expression string into an NFA.