// Test feeder for the vpen driver: moves its controllers in slow circles, presses their buttons and prints haptic pulses.
#include "vpen_shm.h"
#include "shm_compat.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static const double kPi = 3.14159265358979;
static const double kCycleSeconds = 2.0;

static double now_s() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static float ramp(double phase) {
	return (float)(0.5 - 0.5 * std::cos(2.0 * kPi * phase));
}

// One slot's pose and inputs at time t; haptic_seq belongs to the driver and is left alone.
static void fill_pen(vpen_pen &p, uint32_t i, uint32_t count, double t, uint32_t press) {
	double a = t * 0.8 + i * kPi;
	p.px = (float)(0.5 * i - 0.25 * (count - 1) + 0.1 * std::cos(a));
	p.py = (float)(1.2 + 0.1 * std::sin(a));
	p.pz = -0.5f;
	p.qw = 1;
	p.qx = p.qy = p.qz = 0;
	double phase = std::fmod(t / kCycleSeconds + 0.5 * i, 1.0);
	p.trigger = (press & VPEN_BTN_TRIGGER_CLICK) ? ramp(phase) : 0.0f;
	p.grip = (press & VPEN_BTN_GRIP_CLICK) ? ramp(std::fmod(phase + 0.25, 1.0)) : 0.0f;
	uint32_t b = 0;
	if (p.trigger >= 0.9f)
		b |= VPEN_BTN_TRIGGER_CLICK;
	if (p.trigger > 0.05f)
		b |= VPEN_BTN_TRIGGER_TOUCH;
	if (p.grip >= 0.9f)
		b |= VPEN_BTN_GRIP_CLICK;
	if ((press & VPEN_BTN_MENU) && phase >= 0.45 && phase < 0.55)
		b |= VPEN_BTN_MENU;
	if ((press & VPEN_BTN_SYSTEM) && phase < 0.05)
		b |= VPEN_BTN_SYSTEM;
	p.buttons = b;
}

static void write_frame(vpen_shared *m, uint32_t count, double t, uint32_t press) {
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);
	m->count = count;
	m->t_write = t;
	for (uint32_t i = 0; i < count; i++)
		fill_pen(m->pen[i], i, count, t, press);
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);
}

// Seqlock read as the driver does it; false when no stable snapshot was taken.
static bool read_frame(const vpen_shared *m, vpen_shared *out) {
	for (int tries = 0; tries < 8; tries++) {
		uint64_t s0 = __atomic_load_n(&m->seq, __ATOMIC_ACQUIRE);
		if (s0 & 1)
			continue;
		std::memcpy(out, (const void *)m, sizeof(*out));
		if (__atomic_load_n(&m->seq, __ATOMIC_ACQUIRE) == s0)
			return true;
	}
	return false;
}

static bool header_ok(const vpen_shared *m) {
	return m->magic == VPEN_MAGIC && m->version == VPEN_VERSION;
}

// Pulses the driver has reported on a slot since the last call.
static uint32_t take_pulses(const vpen_shared *m, uint32_t slot, uint32_t *seen) {
	uint32_t cur = __atomic_load_n(&m->pen[slot].haptic_seq, __ATOMIC_ACQUIRE);
	uint32_t n = cur - seen[slot];
	seen[slot] = cur;
	return n;
}

static int check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	return ok ? 0 : 1;
}

static int selftest(vpen_shared *m) {
	int fails = 0;
	vpen_shared snap;
	uint32_t all = VPEN_BTN_TRIGGER_CLICK | VPEN_BTN_GRIP_CLICK | VPEN_BTN_MENU | VPEN_BTN_SYSTEM;
	write_frame(m, 2, 0.0, all);
	bool ok = read_frame(m, &snap) && snap.count == 2 && snap.pen[0].trigger == 0.0f &&
			snap.pen[0].buttons == VPEN_BTN_SYSTEM && snap.pen[1].trigger == 1.0f &&
			snap.pen[1].buttons == (VPEN_BTN_TRIGGER_CLICK | VPEN_BTN_TRIGGER_TOUCH | VPEN_BTN_MENU);
	fails += check(ok, "positive: a stable frame reads back, slot 1 half a cycle after slot 0");
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);
	fails += check(!read_frame(m, &snap), "negative: a frame mid-write is refused");
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);

	fails += check(header_ok(m), "positive: the mapped header matches vpen_shm.h");
	vpen_shared stale = *m;
	stale.version = VPEN_VERSION + 1;
	fails += check(!header_ok(&stale), "negative: a header of another version is refused");

	uint32_t seen[VPEN_MAX] = {};
	take_pulses(m, 1, seen);
	__atomic_add_fetch(&m->pen[1].haptic_seq, 1, __ATOMIC_RELEASE);
	write_frame(m, 2, 1.0, all);
	fails += check(take_pulses(m, 1, seen) == 1, "positive: a driver pulse survives a frame write and is counted once");
	fails += check(take_pulses(m, 1, seen) == 0, "negative: no pulse counts zero");

	write_frame(m, 0, 2.0, 0);
	return fails ? 1 : 0;
}

static bool parse_buttons(const char *list, uint32_t *press) {
	static const char *const names[] = {"trigger", "grip", "menu", "system"};
	static const uint32_t bits[] = {VPEN_BTN_TRIGGER_CLICK, VPEN_BTN_GRIP_CLICK, VPEN_BTN_MENU, VPEN_BTN_SYSTEM};
	*press = 0;
	const char *s = list;
	while (*s) {
		size_t n = std::strcspn(s, ",");
		bool found = false;
		for (int k = 0; k < 4; k++) {
			if (std::strlen(names[k]) == n && std::strncmp(s, names[k], n) == 0) {
				*press |= bits[k];
				found = true;
			}
		}
		if (!found && n)
			return false;
		s += n;
		if (*s == ',')
			s++;
	}
	return true;
}

int main(int argc, char **argv) {
	uint32_t count = 2;
	uint32_t press = VPEN_BTN_TRIGGER_CLICK | VPEN_BTN_GRIP_CLICK;
	double seconds = 0;
	bool test = false;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--selftest"))
			test = true;
		else if (!std::strcmp(argv[i], "--count") && i + 1 < argc)
			count = (uint32_t)std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
			seconds = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--buttons") && i + 1 < argc && parse_buttons(argv[i + 1], &press))
			i++;
		else {
			std::fprintf(stderr, "usage: vpen_feeder [--count N] [--seconds S] [--buttons trigger,grip,menu,system] [--selftest]\n");
			return 2;
		}
	}
	if (count > VPEN_MAX)
		count = VPEN_MAX;
	vpen_shared *m = (vpen_shared *)vpen_shm_map(sizeof(vpen_shared));
	if (!m) {
		std::fprintf(stderr, "vpen_feeder: cannot map the vpen shared memory\n");
		return 2;
	}
	if (m->magic != VPEN_MAGIC) {
		std::memset(m, 0, sizeof(*m));
		m->magic = VPEN_MAGIC;
		m->version = VPEN_VERSION;
	}
	if (!header_ok(m)) {
		std::fprintf(stderr, "vpen_feeder: the mapping is vpen_shm version %u, this feeder is %u\n", m->version, VPEN_VERSION);
		return 3;
	}
	if (test)
		return selftest(m);

	uint32_t seen[VPEN_MAX] = {};
	for (uint32_t i = 0; i < count; i++)
		take_pulses(m, i, seen);
	std::printf("vpen_feeder: driving %u controllers, Ctrl+C to stop\n", count);
	double t0 = now_s();
	uint64_t frames = 0;
	for (;;) {
		double t = now_s();
		if (seconds > 0 && t - t0 >= seconds)
			break;
		write_frame(m, count, t, press);
		frames++;
		for (uint32_t i = 0; i < count; i++) {
			uint32_t n = take_pulses(m, i, seen);
			if (n)
				std::printf("vpen_feeder: slot %u haptic x%u\n", i, n);
		}
		std::this_thread::sleep_for(std::chrono::microseconds(11111));
	}
	std::printf("vpen_feeder: %llu frames over %.2f s\n", (unsigned long long)frames, now_s() - t0);
	vpen_shm_unmap(m, sizeof(vpen_shared));
	return 0;
}
