/* Lab 5 host tests: build and run with `make` in this folder. */
#include "check.h"

int check_failures;
int check_count;

void test_netstate_all(void);
void test_raft_proto_all(void);
void test_raft_all(void);

int main(void)
{
	test_netstate_all();
	test_raft_proto_all();
	test_raft_all();
	printf("\n%d checks, %d failed\n", check_count, check_failures);
	return check_failures ? 1 : 0;
}
