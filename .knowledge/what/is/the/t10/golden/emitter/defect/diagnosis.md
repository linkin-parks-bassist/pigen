---
status: green
revised_at: "2026-11-15T14:12:21+11:00"
---

The t10-golden fixture currently passes; it does not identify an active emitter failure. tests/sv_emit_test.c compares the whole emitted design byte-for-byte, including opaque leading/trailing comments, resolved two-state declarations, a sized signed conversion, concatenation, an equation, a legacy positional instance and a clocked update. make sv-emit-test T=t10-golden runs this focused witness.

The fixture and renderer agree on these current constraints:

- Port alignment uses bounded padding; scalar ports retain a bit/logic type chosen from RTL state domain.
- The flat output walk skips module child ranges after rendering them inside their owning module.
- Packed ranges, commas and line endings come from the terminal declaration/layout rules.
- Concatenation renders `{c,e}` without inserted spaces.
- The trailing opaque span includes its source newline; copying an opaque span does not invent one.
- Empty-design success returns an allocated empty string. t10-skeleton checks both the return value and non-NULL string ownership.

The golden's `instance #(8) (.clk(clk))` exercises the legacy record fallback. Production primitives and generated children carry explicit definitions, parameter/port metadata and final names; they are not restricted to this spelling (what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md).

These facts are checked against the current fixture and renderer, rather than inferred from old draft commits or hidden test packets. Broader rendering rules and finite coverage limits are owned by what/is/the/terminal/systemverilog/emitter/contract.md and what/is/elastic/rtl/task/ten.md.
