/* Lab 5 extra credit: publish the Raft log to Redis. See uplink.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "netstate.h"
#include "uplink.h"

#define HEARTBEAT_MS    1000
#define HEARTBEAT_TTL_S "5"
#define RETRY_MS        1000
#define IO_TIMEOUT_S    3
#define LOG_MAXLEN      "10000"   /* stream trimmed to about this many entries */

struct entry {
	uint64_t seq;
	unsigned index;
	unsigned term;
	uint8_t event;
	bool replayed;
	int64_t global_us;
	int64_t wall_ms;
};

/* Shared with the cockpit's threads, under mu */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static struct entry q[UPLINK_QUEUE];
static unsigned q_head, q_count;
static uint64_t q_seq;
static struct uplink_status status;
static bool status_set;
static bool connected;
static unsigned acked_index;     /* highest index known to be in Redis */
static unsigned n_published, n_replayed, n_skipped;
static uint64_t n_dropped, n_connects;

static bool enabled;
static char host[128], port[16], password[128];
static char run_id[17];

/* Owned by the uplink thread */
static int fd = -1;
static char rbuf[4096];
static size_t rlen, rpos;

static int64_t wall_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------------- producer side (cockpit threads) ---------------- */

void uplink_entry(unsigned index, unsigned term, uint8_t event, bool replayed,
		  int64_t global_us)
{
	if (!enabled) {
		return;
	}
	pthread_mutex_lock(&mu);
	if (q_count == UPLINK_QUEUE) {
		/* Keep the newest UPLINK_QUEUE entries */
		q_head = (q_head + 1) % UPLINK_QUEUE;
		q_count--;
		n_dropped++;
	}
	q[(q_head + q_count) % UPLINK_QUEUE] = (struct entry){
		.seq = ++q_seq, .index = index, .term = term, .event = event,
		.replayed = replayed, .global_us = global_us, .wall_ms = wall_ms(),
	};
	q_count++;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mu);
}

void uplink_status(const struct uplink_status *s)
{
	if (!enabled) {
		return;
	}
	pthread_mutex_lock(&mu);
	status = *s;
	status_set = true;
	pthread_mutex_unlock(&mu);
}

void uplink_describe(char *buf, size_t n)
{
	if (!enabled) {
		snprintf(buf, n, "uplink off");
		return;
	}
	pthread_mutex_lock(&mu);
	snprintf(buf, n, "uplink %s: queued %u, published %u (replayed %u, already there %u), "
		 "dropped %llu, connects %llu",
		 connected ? "up" : "DOWN", q_count, n_published, n_replayed, n_skipped,
		 (unsigned long long)n_dropped, (unsigned long long)n_connects);
	pthread_mutex_unlock(&mu);
}

/* ---------------- Redis protocol (RESP), uplink thread only ---------------- */

static void disconnect(void)
{
	if (fd >= 0) {
		close(fd);
	}
	fd = -1;
	rlen = rpos = 0;
	pthread_mutex_lock(&mu);
	connected = false;
	pthread_mutex_unlock(&mu);
}

static int write_all(const char *p, size_t n)
{
	while (n) {
		ssize_t w = send(fd, p, n, MSG_NOSIGNAL);

		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

/* One command as a RESP array of bulk strings */
static int send_cmd(int argc, const char *const *argv)
{
	char buf[4096];
	size_t n = (size_t)snprintf(buf, sizeof(buf), "*%d\r\n", argc);

	for (int i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]);

		if (n + len + 32 > sizeof(buf)) {
			return -1;
		}
		n += (size_t)snprintf(buf + n, sizeof(buf) - n, "$%zu\r\n", len);
		memcpy(buf + n, argv[i], len);
		n += len;
		buf[n++] = '\r';
		buf[n++] = '\n';
	}
	return write_all(buf, n);
}

static int get_byte(void)
{
	if (rpos == rlen) {
		ssize_t r = recv(fd, rbuf, sizeof(rbuf), 0);

		if (r <= 0) {
			return -1;
		}
		rlen = (size_t)r;
		rpos = 0;
	}
	return (unsigned char)rbuf[rpos++];
}

static int read_line(char *out, size_t n)
{
	size_t i = 0;

	for (;;) {
		int c = get_byte();

		if (c < 0) {
			return -1;
		}
		if (c == '\r') {
			if (get_byte() != '\n') {
				return -1;
			}
			break;
		}
		if (i + 1 < n) {
			out[i++] = (char)c;
		}
	}
	out[i] = '\0';
	return 0;
}

/* One reply: returns its type ('+', '-', ':' or '$'), or -1 on an I/O or
 * protocol error. The value goes to out; *nil marks a nil bulk string. */
static int read_reply(char *out, size_t n, bool *nil)
{
	char line[512];

	*nil = false;
	if (read_line(line, sizeof(line)) < 0) {
		return -1;
	}
	switch (line[0]) {
	case '+':
	case '-':
	case ':':
		snprintf(out, n, "%s", line + 1);
		return line[0];
	case '$': {
		long len = strtol(line + 1, NULL, 10);

		out[0] = '\0';
		if (len < 0) {
			*nil = true;
			return '$';
		}
		for (long i = 0; i < len; i++) {
			int c = get_byte();

			if (c < 0) {
				return -1;
			}
			if ((size_t)i + 1 < n) {
				out[i] = (char)c;
				out[i + 1] = '\0';
			}
		}
		if (get_byte() != '\r' || get_byte() != '\n') {
			return -1;
		}
		return '$';
	}
	default:
		return -1;   /* arrays are never asked for */
	}
}

/* Command and its reply. Error replies are logged and returned as '-'. */
static int call(int argc, const char *const *argv, char *out, size_t n, bool *nil)
{
	char scratch[256];
	bool scratch_nil;

	if (!out) {
		out = scratch;
		n = sizeof(scratch);
	}
	if (!nil) {
		nil = &scratch_nil;
	}
	if (send_cmd(argc, argv) < 0) {
		return -1;
	}
	int t = read_reply(out, n, nil);

	if (t == '-') {
		printf("uplink: redis error on %s: %s\n", argv[0], out);
	}
	return t;
}

static int do_connect(void)
{
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *res, *ai;
	struct timeval tv = { .tv_sec = IO_TIMEOUT_S };
	int one = 1;

	if (getaddrinfo(host, port, &hints, &res) != 0) {
		return -1;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			continue;
		}
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		return -1;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (password[0]) {
		const char *auth[] = { "AUTH", password };

		if (call(2, auth, NULL, 0, NULL) != '+') {
			disconnect();
			return -1;
		}
	}
	pthread_mutex_lock(&mu);
	connected = true;
	n_connects++;
	pthread_mutex_unlock(&mu);
	printf("uplink: connected to %s:%s (run %s)\n", host, port, run_id);
	return 0;
}

/* ---------------- publishing, uplink thread only ---------------- */

/* Returns 0 when the entry is done (published, already there, or refused by
 * Redis), -1 on an I/O error (reconnect and retry the same entry). */
static int publish_entry(const struct entry *e)
{
	char idx[12], term[8], code[8], g[24], w[24], name[40], have[32], id[64];
	bool nil;

	snprintf(idx, sizeof(idx), "%u", e->index);
	snprintf(term, sizeof(term), "%u", e->term);
	snprintf(code, sizeof(code), "0x%02x", e->event);
	snprintf(g, sizeof(g), "%lld", (long long)e->global_us);
	snprintf(w, sizeof(w), "%lld", (long long)e->wall_ms);
	raft_ev_name(e->event, name, sizeof(name));

	/* Entry 1 committed live (not replayed) starts a new vehicle log, after
	 * a cold restart of all three nodes: forget the old indexes */
	if (e->index == 1 && !e->replayed) {
		const char *del[] = { "DEL", "vehicle:index" };

		if (call(2, del, NULL, 0, NULL) < 0) {
			return -1;
		}
	}

	/* Already published (before an outage, or by an earlier run)? */
	const char *hget[] = { "HGET", "vehicle:index", idx };
	int t = call(3, hget, have, sizeof(have), &nil);

	if (t < 0) {
		return -1;
	}
	if (t == '$' && !nil && strcmp(have, term) == 0) {
		pthread_mutex_lock(&mu);
		n_skipped++;
		if (e->index > acked_index) {
			acked_index = e->index;
		}
		pthread_mutex_unlock(&mu);
		return 0;
	}

	const char *xadd[] = {
		"XADD", "vehicle:log", "MAXLEN", "~", LOG_MAXLEN, "*",
		"run", run_id, "term", term, "index", idx, "type", name, "code", code,
		"replayed", e->replayed ? "1" : "0", "global_us", g, "wall_ms", w,
	};

	t = call(22, xadd, id, sizeof(id), &nil);
	if (t < 0) {
		return -1;
	}
	if (t != '$') {
		return 0;   /* refused (logged): don't retry forever */
	}
	const char *hset_index[] = { "HSET", "vehicle:index", idx, term };
	const char *hset_up[] = { "HSET", "vehicle:uplink", "run", run_id, "last_index", idx,
				  "last_id", id };

	if (call(4, hset_index, NULL, 0, NULL) < 0 || call(8, hset_up, NULL, 0, NULL) < 0) {
		return -1;
	}
	pthread_mutex_lock(&mu);
	n_published++;
	if (e->replayed) {
		n_replayed++;
	}
	if (e->index > acked_index) {
		acked_index = e->index;
	}
	pthread_mutex_unlock(&mu);
	return 0;
}

static int publish_heartbeat(const struct uplink_status *s)
{
	char wms[24], gus[24], term[8], leader[8], commit[12], pub[12], held[12], rep[12];
	char drift[16], err[16], queued[12], dropped[24];

	pthread_mutex_lock(&mu);
	unsigned acked = acked_index, replayed = n_replayed, qn = q_count;
	unsigned long long dr = (unsigned long long)n_dropped;

	pthread_mutex_unlock(&mu);

	snprintf(wms, sizeof(wms), "%lld", (long long)wall_ms());
	snprintf(gus, sizeof(gus), "%lld", (long long)s->global_us);
	snprintf(term, sizeof(term), "%u", s->term);
	snprintf(leader, sizeof(leader), "%u", s->leader);
	snprintf(commit, sizeof(commit), "%u", s->commit);
	snprintf(pub, sizeof(pub), "%u", acked);
	/* Entries the leader has committed that the mirror does not have yet:
	 * after the cockpit returns, the held entries draining out */
	snprintf(held, sizeof(held), "%u", s->commit > acked ? s->commit - acked : 0);
	snprintf(rep, sizeof(rep), "%u", replayed);
	snprintf(drift, sizeof(drift), "%d", s->drift_ppb);
	snprintf(err, sizeof(err), "%d", s->err_us);
	snprintf(queued, sizeof(queued), "%u", qn);
	snprintf(dropped, sizeof(dropped), "%llu", dr);

	const char *fields[] = {
		"wall_ms", wms, "global_us", gus, "run", run_id, "role", s->role,
		"term", term, "leader", leader, "commit", commit, "published", pub,
		"held", held, "replayed", rep, "sync", s->sync ? "1" : "0",
		"drift_ppb", drift, "err_us", err, "cockpit", s->cockpit_state,
		"queued", queued, "dropped", dropped, "state", s->state_json,
	};
	enum { NF = sizeof(fields) / sizeof(fields[0]) };
	const char *argv[2 + NF];

	argv[0] = "HSET";
	memcpy(&argv[2], fields, sizeof(fields));

	argv[1] = "vehicle:heartbeat";
	if (call(2 + NF, argv, NULL, 0, NULL) < 0) {
		return -1;
	}
	const char *expire[] = { "EXPIRE", "vehicle:heartbeat", HEARTBEAT_TTL_S };

	if (call(3, expire, NULL, 0, NULL) < 0) {
		return -1;
	}
	argv[1] = "vehicle:last";
	return call(2 + NF, argv, NULL, 0, NULL) < 0 ? -1 : 0;
}

static void *uplink_thread(void *arg)
{
	(void)arg;
	/* Below everything else on the Pi: the cockpit must never wait for us */
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);

	int64_t next_hb = 0;
	bool warned = false;

	for (;;) {
		if (fd < 0 && do_connect() < 0) {
			if (!warned) {
				printf("uplink: cannot reach %s:%s, retrying every %d ms\n", host, port,
				       RETRY_MS);
				warned = true;
			}
			usleep(RETRY_MS * 1000);
			continue;
		}
		warned = false;

		struct entry e;
		struct uplink_status st;
		bool have = false, beat = false;

		pthread_mutex_lock(&mu);
		if (q_count) {
			e = q[q_head];
			have = true;
		}
		if (status_set && mono_ms() >= next_hb) {
			st = status;
			beat = true;
		}
		pthread_mutex_unlock(&mu);

		if (have) {
			if (publish_entry(&e) < 0) {
				disconnect();
				continue;
			}
			pthread_mutex_lock(&mu);
			/* Pop it, unless the producer already dropped it (queue full) */
			if (q_count && q[q_head].seq == e.seq) {
				q_head = (q_head + 1) % UPLINK_QUEUE;
				q_count--;
			}
			pthread_mutex_unlock(&mu);
		}
		if (beat) {
			if (publish_heartbeat(&st) < 0) {
				disconnect();
				continue;
			}
			next_hb = mono_ms() + HEARTBEAT_MS;
		}
		if (!have) {
			struct timespec until;

			clock_gettime(CLOCK_REALTIME, &until);
			until.tv_nsec += 200 * 1000000L;
			if (until.tv_nsec >= 1000000000L) {
				until.tv_sec++;
				until.tv_nsec -= 1000000000L;
			}
			pthread_mutex_lock(&mu);
			if (!q_count) {
				pthread_cond_timedwait(&cv, &mu, &until);
			}
			pthread_mutex_unlock(&mu);
		}
	}
	return NULL;
}

void uplink_init(void)
{
	const char *h = getenv("UPLINK_HOST");
	const char *p = getenv("UPLINK_PORT");
	const char *pw = getenv("UPLINK_PASSWORD");

	if (!h || !h[0]) {
		printf("uplink: off (no UPLINK_HOST)\n");
		return;
	}
	snprintf(host, sizeof(host), "%s", h);
	snprintf(port, sizeof(port), "%s", p && p[0] ? p : "6379");
	snprintf(password, sizeof(password), "%s", pw ? pw : "");

	/* A random id per cockpit start, to tell runs apart in the stream */
	unsigned char rnd[8] = { 0 };
	FILE *f = fopen("/dev/urandom", "rb");

	if (f) {
		if (fread(rnd, 1, sizeof(rnd), f) != sizeof(rnd)) {
			memset(rnd, 0, sizeof(rnd));
		}
		fclose(f);
	}
	for (int i = 0; i < 8; i++) {
		snprintf(run_id + 2 * i, 3, "%02x", rnd[i]);
	}

	/* Ordinary scheduling on CPUs 0-1, even if the cockpit runs --rt */
	pthread_attr_t attr;
	struct sched_param sp = { .sched_priority = 0 };
	cpu_set_t cpus;
	pthread_t tid;

	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	pthread_attr_setschedparam(&attr, &sp);
	CPU_ZERO(&cpus);
	CPU_SET(0, &cpus);
	CPU_SET(1, &cpus);
	pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
	enabled = true;
	if (pthread_create(&tid, &attr, uplink_thread, NULL) != 0) {
		perror("uplink: thread");
		enabled = false;
	}
	pthread_attr_destroy(&attr);
	printf("uplink: publishing to %s:%s\n", host, port);
}
