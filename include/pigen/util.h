#ifndef PIGEN_UTIL_H
#define PIGEN_UTIL_H

#include "pigen/model.h"

void pigen_fail(const char *message);
void pigen_warn(const char *message);
void pigen_set_diagnostic_context(const char *path, const char *source);
void pigen_set_diagnostic_position(const char *position);
void *pigen_resize(void *ptr, size_t size);
void pigen_append_range(pigen_string *string, const char *src, size_t length);
void pigen_append(pigen_string *string, const char *src);
void pigen_append_format(pigen_string *string, const char *format, ...);
char *pigen_copy_range(const char *src, size_t length);
int pigen_is_identifier_char(int c);
int pigen_is_word(const char *src, size_t length, const char *word);
const pigen_type_descriptor *pigen_type_descriptor_for_kind(char kind);
char pigen_type_kind_for_keyword(const char *word, size_t length);
const char *pigen_skip_spaces(const char *src, const char *end);
const char *pigen_trim_end(const char *start, const char *end);
const char *pigen_skip_opaque(const char *cursor, const char *end);
const char *pigen_skip_trivia(const char *cursor, const char *end);
void pigen_add_primitive(pigen_primitives *primitives, const char *name, size_t name_length, char kind, int is_internal);
pigen_primitive *pigen_find_primitive(pigen_primitives *primitives, const char *name, size_t name_length);
void pigen_set_port_metadata(pigen_primitives *primitives, const char *name, size_t name_length, const char *payload_type, size_t payload_type_length, const char *fifo_depth, size_t fifo_depth_length, int is_output);
void pigen_add_assignment(pigen_assignments *assignments, const char *destination, size_t destination_length, const char *expression, size_t expression_length, const char *guard, size_t guard_length, const char *domain, size_t domain_length, char destination_kind, size_t order);
void pigen_add_assignment_in_group(pigen_assignments *assignments, const char *destination, size_t destination_length, const char *expression, size_t expression_length, const char *guard, size_t guard_length, const char *domain, size_t domain_length, char destination_kind, size_t group, size_t order);
void pigen_add_width_check(pigen_width_checks *checks, const char *lhs, size_t lhs_length, const char *rhs, size_t rhs_length, size_t group);
void pigen_add_clear(pigen_clears *clears, const char *target, size_t target_length, const char *guard, size_t guard_length, const char *domain, size_t domain_length, int action_kind, size_t order);

#endif
