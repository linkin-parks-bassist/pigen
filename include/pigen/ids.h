#ifndef PIGEN_IDS_H
#define PIGEN_IDS_H

#include <stdint.h>

/* Distinct struct types prevent accidental interchange at compile time. */
#define PIGEN_ID_TYPE(name) typedef struct { uint32_t index; } name

PIGEN_ID_TYPE(pigen_source_id);
PIGEN_ID_TYPE(pigen_syntax_id);
PIGEN_ID_TYPE(pigen_scope_id);
PIGEN_ID_TYPE(pigen_symbol_id);
PIGEN_ID_TYPE(pigen_type_id);
PIGEN_ID_TYPE(pigen_expr_id);
PIGEN_ID_TYPE(pigen_transport_id);
PIGEN_ID_TYPE(pigen_clock_domain_id);
PIGEN_ID_TYPE(pigen_transfer_id);
PIGEN_ID_TYPE(pigen_module_id);
PIGEN_ID_TYPE(pigen_pipeline_id);
PIGEN_ID_TYPE(pigen_stage_id);
PIGEN_ID_TYPE(pigen_fsm_id);
PIGEN_ID_TYPE(pigen_fabric_id);
PIGEN_ID_TYPE(pigen_rtl_id);

#undef PIGEN_ID_TYPE

#define PIGEN_INVALID_ID UINT32_MAX

#endif
