# Shared libc++ headers

libc++ is used header only, from an LLVM source tree rather than an install.
Two headers that libc++ normally has CMake generate are checked in instead.

`__assertion_handler` is here because it is the same for both consoles. It is a
copy of `libcxx/vendor/llvm/default_assertion_handler.in` from the LLVM source
tree, which CMake copies without substituting anything.

`__config_site` is per console, in `xbox/oxdk/libcxx-config` and
`xbox360/oxdk360/libcxx-config`. It selects the subset of libc++ that works on
that target, and the two do not agree.

Both are Apache-2.0 WITH LLVM-exception, like the rest of libc++.
