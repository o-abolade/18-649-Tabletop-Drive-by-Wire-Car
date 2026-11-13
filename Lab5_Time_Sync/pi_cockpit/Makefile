# Lab 5 cockpit (N1). Same Raft core as the STM32s: ../common/raft.
CFLAGS ?= -O2 -Wall -Wextra
RAFT := ../common/raft
SRCS := cockpit.c pi_sync.c ../common/can_proto.c \
        $(RAFT)/raft_proto.c $(RAFT)/netstate.c $(RAFT)/raft_log.c $(RAFT)/raft.c \
        $(RAFT)/gtime.c $(RAFT)/refsync.c

cockpit: $(SRCS) state.h pi_sync.h ../common/can_proto.h $(wildcard $(RAFT)/*.h)
	$(CC) $(CFLAGS) -I../common -I$(RAFT) -o $@ $(SRCS) -lgpiod -lpthread

clean:
	rm -f cockpit

.PHONY: clean
