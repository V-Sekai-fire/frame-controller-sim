// Windows test feeder for the vpen driver: animates N companion pens in slow circles through the shared mapping.
#include "vpen_shm.h"
#include "shm_compat.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static double now_s() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static void write_frame(vpen_shared *m, uint32_t count, double t) {
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);
	m->count = count;
	m->t_write = t;
	for (uint32_t i = 0; i < count; i++) {
		vpen_pen &p = m->pen[i];
		double a = t * 0.8 + i * (6.283185307 / (count ? count : 1));
		p.px = (float)(0.25 * std::cos(a) + 0.15 * (int(i) - int(count) / 2));
		p.py = 1.2f + 0.05f * (float)std::sin(a * 2);
		p.pz = (float)(-0.5 + 0.25 * std::sin(a));
		p.qw = 1;
		p.qx = p.qy = p.qz = 0;
		p.trigger = 1.0f;
		p.grip = 0;
		p.buttons = VPEN_BTN_TRIGGER_CLICK | VPEN_BTN_TRIGGER_TOUCH;
	}
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

static int selftest(vpen_shared *m) {
	int fails = 0;
	vpen_shared snap;
	write_frame(m, 3, 1.0);
	bool ok = read_frame(m, &snap) && snap.magic == VPEN_MAGIC && snap.count == 3 && snap.pen[2].trigger == 1.0f;
	std::printf("%s positive: stable frame reads back\n", ok ? "PASS" : "FAIL");
	fails += !ok;

	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL); // leave the writer holding the lock
	bool torn = read_frame(m, &snap);
	std::printf("%s negative: a frame mid-write is refused\n", !torn ? "PASS" : "FAIL");
	fails += torn;
	__atomic_fetch_add(&m->seq, 1, __ATOMIC_ACQ_REL);

	write_frame(m, 0, 2.0);
	return fails ? 1 : 0;
}

int main(int argc, char **argv) {
	uint32_t count = 4;
	bool test = false;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--selftest"))
			test = true;
		else if (!std::strcmp(argv[i], "--count") && i + 1 < argc)
			count = (uint32_t)std::atoi(argv[++i]);
	}
	if (count > VPEN_MAX)
		count = VPEN_MAX;
	vpen_shared *m = (vpen_shared *)vpen_shm_map(sizeof(vpen_shared));
	if (!m) {
		std::fprintf(stderr, "vpen_feeder: cannot map C:\\ProgramData\\vpen\\vpen_controllers.bin\n");
		return 2;
	}
	if (m->magic != VPEN_MAGIC) {
		std::memset(m, 0, sizeof(*m));
		m->magic = VPEN_MAGIC;
		m->version = VPEN_VERSION;
	}
	if (test)
		return selftest(m);
	std::printf("vpen_feeder: animating %u pens at 90 Hz, Ctrl+C to stop\n", count);
	for (;;) {
		write_frame(m, count, now_s());
		std::this_thread::sleep_for(std::chrono::microseconds(11111));
	}
}
