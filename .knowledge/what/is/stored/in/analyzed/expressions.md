---
status: "unverified"
created_at: "2026-09-13T15:04:53+10:00"
scope: "local"
source: "include/pigen/expression_analysis.h; intrinsic expression design Analysis"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:32+10:00"
---

Temporary analyzed nodes retain syntax/kind/literal kind/span/data type/shape/optional constant and typed children. Unary/binary/conditional nodes store full resolutions; conversions store decisions. The temporary arena owns nodes, child sequences, literal states and width constraints. Analysis may idempotently intern type/width catalogue entries but publishes no partial semantic expression or language object.
