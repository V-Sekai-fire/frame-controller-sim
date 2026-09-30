# Dump every XRServer tracker in a live OpenXR session, to see whether the vpen
# companion devices surface -- vpen_0 by its handheld_object role, and vpen_1..3
# (role-less) by serial/name. Reuses xr_main.tscn only to bring up the session.
#   godot --path pen --xr-mode on --script tools/xr_tracker_dump.gd
extends SceneTree

const SCENE := "res://xr_main.tscn"
var _t0 := 0
var _main: Node = null
var _frames := 0
var _last_dump := -1

func _initialize() -> void:
	_t0 = Time.get_ticks_msec()
	var xr = XRServer.find_interface("OpenXR")
	print("dump: OpenXR interface ", "found" if xr != null else "MISSING")
	var ps: PackedScene = load(SCENE)
	if ps == null:
		print("dump: cannot load ", SCENE)
		quit(1)
		return
	_main = ps.instantiate()
	root.add_child(_main)

func _process(_dt: float) -> bool:
	_frames += 1
	var secs := int((Time.get_ticks_msec() - _t0) / 1000.0)
	# Dump at ~3s, 6s, 9s so late-registering trackers show up.
	if secs != _last_dump and (secs == 3 or secs == 6 or secs == 9):
		_last_dump = secs
		_dump(secs)
	if secs >= 11:
		quit(0)
	return false

func _dump(secs: int) -> void:
	var w = _main.get_node_or_null("World") if _main != null else null
	var xr_on = w != null and w.get("xr_on")
	print("=== trackers @ %ds (xr_on=%s) ===" % [secs, str(xr_on)])
	var trackers: Dictionary = XRServer.get_trackers(XRServer.TRACKER_ANY)
	print("  count=", trackers.size())
	for tn in trackers:
		var t = trackers[tn]
		var typ = t.type if t != null else -1
		var desc = t.description if t != null else ""
		print("  name='%s' type=%s desc='%s'" % [str(tn), str(typ), str(desc)])
