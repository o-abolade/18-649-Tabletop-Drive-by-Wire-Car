/* Minimal test helpers for the Lab 5 host tests (no external framework). */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

extern int check_failures;
extern int check_count;

#define CHECK(cond)                                                              \
	do {                                                                     \
		check_count++;                                                   \
		if (!(cond)) {                                                   \
			check_failures++;                                        \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                \
	} while (0)

#define CHECK_EQ(a, b)                                                           \
	do {                                                                     \
		long long check_a_ = (long long)(a), check_b_ = (long long)(b);  \
		check_count++;                                                   \
		if (check_a_ != check_b_) {                                      \
			check_failures++;                                        \
			printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, \
			       __LINE__, #a, #b, check_a_, check_b_);            \
		}                                                                \
	} while (0)

#define RUN(test)                                \
	do {                                     \
		int before_ = check_failures;    \
		test();                          \
		printf("%s %s\n", check_failures == before_ ? "ok  " : "FAIL", #test); \
	} while (0)

#endif /* CHECK_H */
