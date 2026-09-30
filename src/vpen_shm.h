// vpen_shm.h -- the shared-memory contract between the Lean 4 feeder (the
// producer, in the frame-eye-osc shape) and the SteamVR virtual-controller
// driver (the consumer). One writer, one reader, a seqlock so the reader never
// sees a half-written frame: the producer bumps seq to odd, writes the body,
// bumps seq to even; the reader takes seq, reads the body, takes seq again, and
// retries while it is odd or changed.
//
// Each slot is one companion pen -- an extra vive-tracker device the driver
// registers beside the two real hand controllers, so it draws alongside the
// person. The count is a throughput knob: more pens lay more strokes at once, so a
// larger `count` generates geometry faster. VPEN_MAX bounds it to the distinct
// vive-tracker roles the runtime exposes.
#ifndef VPEN_SHM_H
#define VPEN_SHM_H
#include <stdint.h>

#define VPEN_SHM_NAME "/vpen_controllers"
#define VPEN_MAGIC 0x56504E32u // "VPN2"
#define VPEN_VERSION 2u
#define VPEN_MAX 15 // the count of distinct XR_HTCX_vive_tracker_interaction roles

// buttons bitmask
#define VPEN_BTN_TRIGGER_CLICK (1u << 0)
#define VPEN_BTN_TRIGGER_TOUCH (1u << 1)
#define VPEN_BTN_GRIP_CLICK (1u << 2)
#define VPEN_BTN_MENU (1u << 3)
#define VPEN_BTN_SYSTEM (1u << 4)

typedef struct {
	float px, py, pz;     // position in meters, SteamVR world (y up, -z forward)
	float qw, qx, qy, qz; // orientation quaternion
	float trigger;        // 0..1, the pen-down / pressure the SketchTool reads
	float grip;           // 0..1
	uint32_t buttons;     // VPEN_BTN_*
	uint32_t haptic_seq;  // driver bumps this when the app requests a pulse; the feeder reads it back
} vpen_pen;

typedef struct {
	uint32_t magic;   // VPEN_MAGIC
	uint32_t version; // VPEN_VERSION
	uint64_t seq;     // seqlock: odd while writing, even when stable
	double t_write;   // producer monotonic seconds at the last write
	uint32_t count;   // active companion pens, 0..VPEN_MAX; the driver adds this many devices
	uint32_t _pad;
	vpen_pen pen[VPEN_MAX];
} vpen_shared;

#endif
