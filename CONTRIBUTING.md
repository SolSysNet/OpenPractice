# Contributing to OpenPractice

Thanks for helping. Firms decide staffing, billing and fees from OpenPractice's numbers, so this project
cares more about **correct**, **explainable** and **boring** than clever.

## Ground rules

1. **Money and hours are never floating point.** Use `op::Money` (integer cents) and `op::Decimal` (hours,
   rates, percentages). Display code may convert to `float` only to size a progress bar.
2. **Every figure is explainable.** A computed number follows a definition in `calc.hpp` and the README's
   *How the numbers work* table. If you add or change one, update both and add a hand-worked test.
3. **Nothing talks to the network.** No sockets, HTTP, update checks or telemetry, and no build-time
   downloads (`FetchContent`, `ExternalProject`, package managers).
4. **Nothing is silently dropped.** The file parser rejects unknown records and fields. Deleting a record
   that others refer to is blocked or clears the references explicitly; it never leaves a dangling id.
5. **Declare a field once.** New fields go in the record's schema in `model.cpp`; the file format, CSV,
   tables and editors pick them up from there.

## Getting started

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

- On Linux, install GLFW (`libglfw3-dev`); on macOS, `brew install glfw`. Use
  `-DOPENPRACTICE_BUILD_GUI=OFF` to build only the engine, command line and tests.
- Do GUI work in a **Debug** build: Dear ImGui's assertions catch real layout bugs there.
- Test with `openpractice sample scratch.opp`, never with a real firm's file.

## Making changes

- Code style: C++17, 4-space indents, 120 columns, `camelCase` functions, `PascalCase` types, members with a
  trailing underscore. Match the code around you.
- Build warning-free with `-Wall -Wextra -Wpedantic` (or `/W4`).
- In the GUI, never add or remove records while a screen is drawing from that list: queue the change with
  `App::defer()`. Field edits through bound widgets are fine, followed by `changed()`.
- Add tests to `tests/test_main.cpp` for engine changes. Expected values should be worked by hand, with the
  arithmetic in a comment.

## Pull requests

Keep them focused, describe what changed and why, and include before/after screenshots for UI changes.
