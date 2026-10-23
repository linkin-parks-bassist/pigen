---
status: green
revised_at: "2026-09-13T15:07:28+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

The Makefile uses cc with -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude and production linkage -lm. Structured tests compile explicit owner source groups rather than linking them to the prototype executable. This is observed build configuration, not evidence that all tests were run.
