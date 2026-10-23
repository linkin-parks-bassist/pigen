/* Minimal test harness: named sections, failures reported per section.
 *
 * SECTION("name") { ... } runs its block when no section arguments were given
 * or when an argument is a prefix of the name. REQUIRE(expr) failing reports
 * "FAIL: <section> at <file>:<line>: <expr>" and ends that section only; the
 * remaining sections still run. Each section that completes prints
 * "PASS: <section>". check_finish() prints the totals and returns the exit
 * status (nonzero when any section failed or an argument matched nothing).
 * "--list" prints the section names without running them. */
#ifndef PIGEN_TESTS_CHECK_H
#define PIGEN_TESTS_CHECK_H

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__)
/* After a failure the section's locals are abandoned, never reused. */
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

static int check_argc;
static char **check_argv;
static int check_list;
static int check_matched[64];
static const char *check_section;
static int check_section_failed;
static int check_passed;
static int check_failed;
static jmp_buf check_jump;

static inline void check_init(int argc, char **argv)
{
	int i;

	check_argc = argc;
	check_argv = argv;
	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--list"))
			check_list = 1;
}

static inline int check_selected(const char *name)
{
	int i, any = 0, hit = 0;

	for (i = 1; i < check_argc && i < 64; i++) {
		if (!strcmp(check_argv[i], "--list"))
			continue;
		any = 1;
		if (!strncmp(name, check_argv[i], strlen(check_argv[i]))) {
			check_matched[i] = 1;
			hit = 1;
		}
	}
	return !any || hit;
}

static inline int check_begin(const char *name)
{
	if (!check_selected(name))
		return 0;
	if (check_list) {
		puts(name);
		return 0;
	}
	check_section = name;
	check_section_failed = 0;
	return 1;
}

static inline int check_end(void)
{
	if (check_section_failed) {
		check_failed++;
	} else {
		printf("PASS: %s\n", check_section);
		check_passed++;
	}
	fflush(stdout);
	return 0;
}

static inline void check_fail(const char *file, int line, const char *expr)
{
	printf("FAIL: %s at %s:%d: %s\n", check_section, file, line, expr);
	fflush(stdout);
	check_section_failed = 1;
	longjmp(check_jump, 1);
}

static inline int check_finish(void)
{
	int i, unmatched = 0;

	for (i = 1; i < check_argc && i < 64; i++)
		if (strcmp(check_argv[i], "--list") && !check_matched[i]) {
			printf("FAIL: no section matches '%s'\n", check_argv[i]);
			unmatched = 1;
		}
	if (check_list)
		return unmatched;
	printf("%d passed, %d failed\n", check_passed, check_failed);
	return check_failed || unmatched;
}

#define REQUIRE(expr) ((expr) ? (void)0 : check_fail(__FILE__, __LINE__, #expr))

#define SECTION(name) \
	for (int check_once_ = check_begin(name); check_once_; check_once_ = check_end()) \
		if (setjmp(check_jump) == 0)

#endif
