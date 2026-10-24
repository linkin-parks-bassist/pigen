---
status: green
revised_at: "2026-10-25T09:51:21+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Yes. The Makefile pigen: target links the structured source set (32 files) and src/pigen.c main() calls pigen_compile_source. The old text-macro scanning machine is deleted. Corpus .pigen files use the prototype dialect the structured frontend diagnoses by design; those Makefile targets go red as per-subsystem migration debt until each subsystem's structured path covers the spec. Do not add a validator bridge, per-file compiler choice or fallback.
