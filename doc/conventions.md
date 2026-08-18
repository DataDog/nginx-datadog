# Datadog Nginx Module Conventions

This document defines repository-wide conventions for `nginx-datadog`. Apply these conventions to
all new and modified code.

## C++ Version

The project uses C++20.

## Clean Code

- Use meaningful variable and function names.
- Do not use single-letter variable names.
- Avoid abbreviations, unless very common and unambiguous.
- Avoid obvious comments.
- Prefer clearer variable and function names over explanatory comments.
- Keep functions small and focused (<~ 20 lines when practical).
- When practical, place caller functions before callees, so the code can be read from top to bottom.

## C++ Code Style

- Use modern C++ idioms.
- Prefer explicit types. Use `auto` only for very long type names (>~ 50 characters).
- Never use C-style casts.
- Use raw pointers only when absolutely necessary.
- Use C++17 nested namespace syntax.
- Minimize the number of `#include` lines. Do not enforce the include-what-you-use rule.

## Naming Conventions

- class: `class TypeName;`
- class member function: `.member_function();`
- class public member: `int public_member;`
- class private member: `int private_member_;`
- free function: `free_function();`
- function argument: `function(int func_arg);`
- local variable: `int local_var;`
- enumeration: `enum class Color { RED, GREEN, BLUE };`
