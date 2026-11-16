---
status: green
revised_at: "2026-11-17T06:15:57+11:00"
---

Native AXI4-Stream, AXI4-Lite and full AXI4 module inputs/outputs and fabrics are implemented and verified against what/are/the/native/axi/transfer/requirements.md. Broader SystemVerilog feature work is separate unless a concrete interface/fabric use establishes a dependency.

| Requirement | Acceptance evidence |
| --- | --- |
| Native contracts and complete fields | protocol_test.c checks full field layouts and legal DATA widths; native_protocol_test.py supplies an independent standard-pin schema; how/are/native/axi/protocol/contracts/represented.md |
| Module/child/external-IP boundaries and incompatible-connection rejection | Scalar/array/collection hierarchy profiles; external_axi_tests mutates width, role, clock, sideband and address contracts and checks preserved output; how/are/native/axi/endpoints/mapped/to/external/systemverilog/ip.md |
| Independent handshakes, buffering, stalls/reset and DATA types | buf/fifo/skid, four-state/sign profiles, omitted-TDATA Stream and independent scalar-pin foreign queues; how/are/native/axi/payload/states/preserved.md; how/to/write/axi/streams/without/data.md |
| Fabric ownership, legal transactions and response provenance | Colliding/absent IDs, cumulative tags, exclusives, fan-in, mapped switching, real-core bursts/contention and independent target progress; how/to/verify/native/axi/core/fabric/composition.md |
| Parameterized addresses and instance overrides | BASE/STRIDE/WINDOW_BYTES, 40/64-bit routes and overlap rejection; how/does/native/memory/mapped/address/routing/work.md |
| Generated endpoint/child/fabric composition | All-family array, named-scope, external-IP, parameterized-child and typed-standalone profiles; how/are/native/axi/endpoint/arrays/represented.md |

Current verification covers every make verify target in completed phases. The umbrella run /tmp/pigen-axi-boundaries-verify.log ended with SIGTERM/143, which is not a passing command. Before interruption, native_protocol_test.py's final PASS and Make advancing to native_array_test.py were observed in its live output spool; that deleted spool's tail was not retained in the redirected log.

The remaining nine native-protocol recipe scripts each exited 0, and make -j8 --output-sync=target --old-file=native-protocol-test verify exited 0 for the other targets, including 122/122 core cases. Commands, exit codes and logs are recorded in /tmp/pigen-axi-final-batches-4yofyxk0/status.json. All ten native-protocol recipe scripts are accounted for; the excluded target is covered by those script results.

Verified compiler SHA-256 is ccc7dab4b38583ad7f243afa39ebd0cb5c78c9dae444ca529a6f95cd79510d44. Tests ran at source fingerprint 9a6a60cced558d14847b4fe16554b08a5781e36b82bec337567a394fda02621b. Publication removes trailing whitespace in two Python tests with unchanged ASTs (/tmp/pigen-publish-whitespace-check.json), yielding fingerprint 0c717b6513a9bb1d705a73c3b6751841ad8f2da184ebd9f6cba0954fabbfe082. The compiler also passes argument checks, 75 observation simulations under ASan/UBSan and syntax/semantic/resolver/output models (how/are/retained/native/axi/observations/represented.md).

Limits remain explicit: exact contracts except defined routing ID adaptation; no implicit width/protocol conversion or arbitrary foreign pin renaming; synchronous sampled reset is qualified; memory-mapped routing retains global addresses and selects a whole burst by its starting address. These are finite simulation profiles.

Memory-mapped and Stream fabrics use different routing machinery but currently share fabric syntax. A separate memory-mapped spelling remains open. The unfinished general generated-retained experiment is outside production.
