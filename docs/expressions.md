# Expressions

This document illustrates the use of expressions in the your application.

# Introduction

First a few words about expressions. An expression is a string that is evaluated to a value. The value can be a number, a string, a boolean, or a list of values. The expression can be used in the following places:

- In the console window,
- using the `--eval` command,
- and using the `eval(...)` function

# Framework

## FILTER($set, $filter)

The `FILTER(...)` function is used to filter a set of values. The first argument is the set of values, and the second argument is the filter expression. The filter expression is usually an assertion that return true or false. If the assertion is true, the value is kept in the set. If the assertion is false, the value is removed from the set. The filter expression can be any expression that returns a boolean value. The following examples illustrate the use of the `FILTER(...)` function:

### Examples

```js
// Keep only the values that are greater than 10
FILTER([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], $1 > 5) == [6, 7, 8, 9, 10]
```

# Application

## Functions

### MY_FUNCTION($arg1, $arg2)

_This is a function that does something..._

#### Examples

```
MY_FUNCTION(1, 2) => 3
```

_..._
