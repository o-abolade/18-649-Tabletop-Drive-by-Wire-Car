# Lab 5 host tests. Needs a host C compiler (gcc on the Pi, or in WSL:
# `sudo apt install -y gcc make`). Run: make
CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g
RAFT := ../common/raft

SRCS := test_main.c test_netstate.c test_raft_proto.c test_raft.c test_gtime.c test_refsync.c \
        $(RAFT)/netstate.c $(RAFT)/raft_proto.c $(RAFT)/raft_log.c $(RAFT)/raft.c \
        $(RAFT)/gtime.c $(RAFT)/refsync.c

run_tests: $(SRCS) $(wildcard $(RAFT)/*.h) check.h
	$(CC) $(CFLAGS) -I$(RAFT) -o $@ $(SRCS)
	./$@

.PHONY: clean
clean:
	rm -f run_tests
