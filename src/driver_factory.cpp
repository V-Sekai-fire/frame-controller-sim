// The Frame virtual-controller driver: N companion pen devices registered
// beside the two real hand controllers, so the driver draws alongside the
// person for annotation and grading. Each pen is a handed tracker that SteamVR
// draws nothing for, so it never takes a hand role or clutters the view. A
// feeder writes its pose and trigger into shared memory (vpen_shm.h) and
// RunFrame polls it each frame.
// Cross-built on Windows for aarch64 SteamOS; see build.sh.
#include "openvr_driver.h"
#include "vpen_shm.h"
#include "shm_compat.h"

#include <cstdio>
#include <cstring>

using namespace vr;

struct DeviceSpec {
	ETrackedDeviceClass cls;  // Controller or GenericTracker
	const char *type;         // Prop_ControllerType_String (an installed profile)
	const char *profile;      // Prop_InputProfilePath_String
	const char *render_model; // the device's own model, carried in Prop_ModelNumber_String
};

// Highest-marketshare device models, every one a handed tracker: a tracker never
// contends for the person's hand roles, where a controller-class device does. SteamVR
// draws nothing for them ({vpen}hidden); the real model rides in ModelNumber. 15 = the cap.
static const DeviceSpec kDevices[] = {
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_quest2_controller_left"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_quest2_controller_right"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{indexcontroller}valve_controller_knu_1_0_left"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{indexcontroller}valve_controller_knu_1_0_right"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_quest_plus_controller_left"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_quest_plus_controller_right"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{vpen}vr_controller_vive_1_5"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{vpen}vr_controller_vive_1_5"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_rifts_controller_left"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "oculus_rifts_controller_right"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{frame_controller}frame_controller_left"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{frame_controller}frame_controller_right"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{htc}vr_tracker_vive_3_0"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "{htc}vr_tracker_vive_1_0"},
		{TrackedDeviceClass_GenericTracker, "vive_tracker", "{htc}/input/tracker/vive_tracker_handed_profile.json", "generic_tracker"},
};
static const int kNumDevices = (int)(sizeof(kDevices) / sizeof(kDevices[0]));

static void log(const char *msg) {
	if (VRDriverLog())
		VRDriverLog()->Log(msg);
}

// One companion pen: a tracked device whose pose and trigger come from a shm slot.
class CPenDevice : public ITrackedDeviceServerDriver {
public:
	CPenDevice(int slot, const vpen_shared *shm) : m_slot(slot), m_shm(shm) {
		std::snprintf(m_serial, sizeof(m_serial), "vpen_%d", slot);
	}

	EVRInitError Activate(uint32_t id) override {
		m_id = id;
		const DeviceSpec &sp = kDevices[m_slot < kNumDevices ? m_slot : kNumDevices - 1];
		m_is_controller = sp.cls == TrackedDeviceClass_Controller;
		PropertyContainerHandle_t c = VRProperties()->TrackedDeviceToPropertyContainer(id);
		VRProperties()->SetStringProperty(c, Prop_ModelNumber_String, sp.render_model);
		VRProperties()->SetStringProperty(c, Prop_SerialNumber_String, m_serial);
		VRProperties()->SetStringProperty(c, Prop_ControllerType_String, sp.type);
		VRProperties()->SetStringProperty(c, Prop_InputProfilePath_String, sp.profile);
		VRProperties()->SetStringProperty(c, Prop_RenderModelName_String, "{vpen}hidden");
		VRProperties()->SetInt32Property(c, Prop_DeviceClass_Int32, sp.cls);
		// OptOut of the hand roles: the pens are read by serial through OpenVR, and
		// this keeps them from contending for the person's left/right controllers.
		VRProperties()->SetInt32Property(c, Prop_ControllerRoleHint_Int32, TrackedControllerRole_OptOut);
		// Lowest hand-selection priority so the person's real controllers always win
		// the hand roles and UI focus over the companions (high numbers win, 7002).
		VRProperties()->SetInt32Property(c, Prop_ControllerHandSelectionPriority_Int32, -1);
		VRProperties()->SetBoolProperty(c, Prop_NeverTracked_Bool, false);
		if (!m_is_controller)
			return VRInitError_None; // trackers are pose-only

		// Inputs matching the controller's profile so its bindings resolve. The feeder
		// drives trigger/grip/menu/system; the rest exist for the bindings.
		VRDriverInput()->CreateScalarComponent(c, "/input/trigger/value", &m_trigger,
				VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
		VRDriverInput()->CreateScalarComponent(c, "/input/grip/value", &m_grip,
				VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
		VRDriverInput()->CreateBooleanComponent(c, "/input/trigger/click", &m_trigger_click);
		VRDriverInput()->CreateBooleanComponent(c, "/input/grip/click", &m_grip_click);
		VRDriverInput()->CreateBooleanComponent(c, "/input/menu/click", &m_menu);
		VRDriverInput()->CreateBooleanComponent(c, "/input/system/click", &m_system);
		VRDriverInput()->CreateHapticComponent(c, "/output/haptic", &m_haptic);

		static const char *const extra_bool[] = {
				"/input/trigger/touch", "/input/grip/touch",
				"/input/thumbstick/click", "/input/thumbstick/touch",
				"/input/a/click", "/input/a/touch", "/input/b/click", "/input/b/touch",
				"/input/x/click", "/input/x/touch", "/input/y/click", "/input/y/touch",
				"/input/view/click", "/input/view/touch", "/input/bumper/click", "/input/bumper/touch",
				"/input/system/touch", "/input/menu/touch", "/input/thumbrest/touch",
				"/input/dpad_up/click", "/input/dpad_up/touch", "/input/dpad_down/click", "/input/dpad_down/touch",
				"/input/dpad_left/click", "/input/dpad_left/touch", "/input/dpad_right/click", "/input/dpad_right/touch"};
		for (const char *p : extra_bool) {
			VRInputComponentHandle_t h = 0;
			VRDriverInput()->CreateBooleanComponent(c, p, &h);
		}
		VRInputComponentHandle_t hx = 0, hy = 0;
		VRDriverInput()->CreateScalarComponent(c, "/input/thumbstick/x", &hx,
				VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);
		VRDriverInput()->CreateScalarComponent(c, "/input/thumbstick/y", &hy,
				VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);
		return VRInitError_None;
	}

	void Deactivate() override { m_id = k_unTrackedDeviceIndexInvalid; }
	void EnterStandby() override {}
	void *GetComponent(const char *) override { return nullptr; }
	void DebugRequest(const char *, char *buf, uint32_t size) override {
		if (size)
			buf[0] = 0;
	}

	DriverPose_t GetPose() override {
		DriverPose_t p = {};
		p.qWorldFromDriverRotation.w = 1;
		p.qDriverFromHeadRotation.w = 1;
		p.deviceIsConnected = true;
		p.poseIsValid = true;
		p.result = TrackingResult_Running_OK;
		// Static fallback so the pen enumerates as a connected tracker before any
		// feeder exists: a small fan in front of the origin, one step per slot.
		p.vecPosition[0] = -0.3 + 0.2 * m_slot;
		p.vecPosition[1] = 1.2;
		p.vecPosition[2] = -0.4;
		p.qRotation.w = 1;
		if (m_shm && m_slot < (int)m_shm->count) {
			const vpen_pen &s = m_shm->pen[m_slot];
			p.vecPosition[0] = s.px;
			p.vecPosition[1] = s.py;
			p.vecPosition[2] = s.pz;
			p.qRotation.w = s.qw;
			p.qRotation.x = s.qx;
			p.qRotation.y = s.qy;
			p.qRotation.z = s.qz;
		}
		return p;
	}

	// Called by the provider each frame with a stable copy of this slot.
	void Update(const vpen_pen &s, bool active) {
		if (m_id == k_unTrackedDeviceIndexInvalid)
			return;
		DriverPose_t pose = GetPose();
		VRServerDriverHost()->TrackedDevicePoseUpdated(m_id, pose, sizeof(pose));
		if (!active || !m_is_controller)
			return;
		VRDriverInput()->UpdateScalarComponent(m_trigger, s.trigger, 0);
		VRDriverInput()->UpdateScalarComponent(m_grip, s.grip, 0);
		VRDriverInput()->UpdateBooleanComponent(m_trigger_click, (s.buttons & VPEN_BTN_TRIGGER_CLICK) != 0, 0);
		VRDriverInput()->UpdateBooleanComponent(m_grip_click, (s.buttons & VPEN_BTN_GRIP_CLICK) != 0, 0);
		VRDriverInput()->UpdateBooleanComponent(m_menu, (s.buttons & VPEN_BTN_MENU) != 0, 0);
		VRDriverInput()->UpdateBooleanComponent(m_system, (s.buttons & VPEN_BTN_SYSTEM) != 0, 0);
	}

	const char *Serial() const { return m_serial; }

private:
	int m_slot;
	const vpen_shared *m_shm;
	uint32_t m_id = k_unTrackedDeviceIndexInvalid;
	bool m_is_controller = false;
	char m_serial[32];
	VRInputComponentHandle_t m_trigger = 0, m_grip = 0;
	VRInputComponentHandle_t m_trigger_click = 0, m_grip_click = 0, m_menu = 0, m_system = 0;
	VRInputComponentHandle_t m_haptic = 0;
};

class CServerDriver : public IServerTrackedDeviceProvider {
public:
	EVRInitError Init(IVRDriverContext *ctx) override {
		VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
		m_shm = (vpen_shared *)vpen_shm_map(sizeof(vpen_shared));
		if (m_shm && m_shm->magic != VPEN_MAGIC) {
			// First mapping: stamp the header so the feeder finds a live segment.
			std::memset(m_shm, 0, sizeof(vpen_shared));
			m_shm->magic = VPEN_MAGIC;
			m_shm->version = VPEN_VERSION;
		}
		// One device per kDevices entry (the marketshare controller pairs + trackers).
		m_count = kNumDevices < VPEN_MAX ? kNumDevices : VPEN_MAX;
		for (int i = 0; i < m_count; i++) {
			m_pen[i] = new CPenDevice(i, m_shm);
			VRServerDriverHost()->TrackedDeviceAdded(m_pen[i]->Serial(), kDevices[i].cls, m_pen[i]);
		}
		char msg[64];
		std::snprintf(msg, sizeof(msg), "vpen: %d companion devices added, shm %s", m_count, m_shm ? "mapped" : "FAILED");
		log(msg);
		return VRInitError_None;
	}

	void Cleanup() override {
		for (int i = 0; i < m_count; i++) {
			delete m_pen[i];
			m_pen[i] = nullptr;
		}
		vpen_shm_unmap(m_shm, sizeof(vpen_shared));
		m_shm = nullptr;
		VR_CLEANUP_SERVER_DRIVER_CONTEXT();
	}

	const char *const *GetInterfaceVersions() override { return k_InterfaceVersions; }

	void RunFrame() override {
		if (!m_shm)
			return;
		// seqlock read: retry while the writer holds it (odd) or it moves under us.
		vpen_shared snap;
		for (int tries = 0; tries < 8; tries++) {
			uint64_t s0 = __atomic_load_n(&m_shm->seq, __ATOMIC_ACQUIRE);
			if (s0 & 1)
				continue;
			std::memcpy(&snap, (const void *)m_shm, sizeof(snap));
			uint64_t s1 = __atomic_load_n(&m_shm->seq, __ATOMIC_ACQUIRE);
			if (s0 == s1)
				break;
		}
		for (int i = 0; i < m_count; i++) {
			bool active = (uint32_t)i < snap.count;
			m_pen[i]->Update(snap.pen[i], active);
		}
	}

	bool ShouldBlockStandbyMode() override { return false; }
	void EnterStandby() override {}
	void LeaveStandby() override {}

private:
	vpen_shared *m_shm = nullptr;
	int m_count = 0;
	CPenDevice *m_pen[VPEN_MAX] = {};
};

static CServerDriver g_server;

extern "C" __attribute__((visibility("default"))) void *
HmdDriverFactory(const char *pInterfaceName, int *pReturnCode) {
	if (pInterfaceName && std::strcmp(pInterfaceName, IServerTrackedDeviceProvider_Version) == 0)
		return &g_server;
	if (pReturnCode)
		*pReturnCode = VRInitError_Init_InterfaceNotFound;
	return nullptr;
}
