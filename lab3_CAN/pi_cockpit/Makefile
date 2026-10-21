CFLAGS ?= -O2 -Wall -Wextra

cockpit: cockpit.c state.h ../common/can_proto.c ../common/can_proto.h
	$(CC) $(CFLAGS) -I../common -o $@ cockpit.c ../common/can_proto.c -lgpiod

clean:
	rm -f cockpit

.PHONY: clean
