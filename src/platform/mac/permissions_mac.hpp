#pragma once
// TCC permission probes and the corresponding System Settings deep links.
//
// This app needs three unrelated grants, each with its own prompt and its own
// failure mode when missing:
//   Microphone       — recording; without it ma_device_start captures silence
//   Input Monitoring — the CGEventTap; CGEventTapCreate returns NULL without it
//   Accessibility    — CGEventPost; the synthetic Cmd+V silently no-ops
//
// All grants are keyed to the code signature, so ad-hoc/unsigned rebuilds lose
// them. Silently doing nothing is the default failure mode of getting this
// wrong, hence the explicit probes and the actionable settings links.

#include <string>

namespace mac::permissions {

// Accessibility (CGEventPost). `prompt` shows the system's one-time
// "grant access" dialog; it only appears once per code signature.
bool accessibility_trusted(bool prompt);

// Input Monitoring (CGEventTap). Preflight does not prompt; request() shows
// the system dialog and returns the (usually still false) immediate result —
// the user has to toggle the switch in System Settings either way.
bool input_monitoring_granted();
bool request_input_monitoring();

// Microphone. Asks up front so the first recording is not silently empty
// while the prompt is still on screen; the callback runs on an arbitrary
// thread, so it only logs. Safe to call when the status is already decided.
void request_microphone_access();
bool microphone_denied();

// Open the relevant System Settings pane.
void open_accessibility_settings();
void open_input_monitoring_settings();
void open_microphone_settings();

} // namespace mac::permissions
