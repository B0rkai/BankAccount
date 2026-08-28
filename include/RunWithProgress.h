#pragma once
#include "wx/string.h"
#include <functional>
#include <string>

class wxWindow;

// Runs 'work' on a background thread while showing an app-modal, indeterminate wxProgressDialog
// on the main thread, so a slow synchronous backend call (network fetch, heavy parse, ...) no
// longer freezes the whole UI ("Not Responding") for its duration. Blocks the calling (main)
// thread until 'work' completes - same as calling it directly would from the caller's point of
// view - the only difference is the UI keeps repainting/responding meanwhile.
//
// 'work' is called on the background thread and receives a thread-safe report_phase callback it
// can call (from that same background thread) with a short human-readable description of what's
// currently happening; the dialog's message is updated with the latest one on each pulse.
void RunBlockingWithProgress(wxWindow* parent, const wxString& title, const wxString& initial_message,
                              const std::function<void(const std::function<void(const std::string&)>&)>& work);
