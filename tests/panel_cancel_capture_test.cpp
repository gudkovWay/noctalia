#include "shell/panel/panel_cancel_capture.h"

#include <cstdio>
#include <print>

namespace {
  bool expect(bool condition, const char* message) {
    if (!condition) {
      std::println(stderr, "panel_cancel_capture_test: {}", message);
    }
    return condition;
  }
}

int main() {
  using Decision = PanelCancelCapture::Decision;
  PanelCancelCapture capture;
  bool ok = expect(capture.handle(true) == Decision::Pass, "normal Cancel must close through the host");
  ok = expect(capture.handle(false) == Decision::Pass, "uncaptured release must remain with the host") && ok;

  capture.setEnabled(true);
  ok = expect(capture.handle(true) == Decision::Deliver, "first modal Esc must reach the script") && ok;
  ok = expect(capture.handle(true) == Decision::Repeat, "held Esc must not cancel twice") && ok;
  capture.setEnabled(false); // Script has returned to NORMAL while Esc is still physically down.
  ok = expect(capture.handle(true) == Decision::Repeat, "repeat after leaving hints must not close the panel") && ok;
  ok = expect(capture.handle(false) == Decision::Deliver, "captured Esc release must clear the script key latch") && ok;
  ok = expect(capture.handle(true) == Decision::Pass, "a fresh second Esc must close normally") && ok;

  capture.setEnabled(true);
  ok = expect(capture.handle(true) == Decision::Deliver, "re-entering hints must capture a fresh Esc") && ok;
  capture.reset(); // Close, reload or script failure: never trap the owner in a failed modal.
  ok = expect(capture.handle(true) == Decision::Pass, "reset must restore host dismissal even while held") && ok;
  capture.setEnabled(true);
  ok = expect(capture.handle(true) == Decision::Deliver, "reset must clear the old press before reopening") && ok;
  ok = expect(capture.handle(false) == Decision::Deliver, "release while modal remains active must be delivered") && ok;
  ok = expect(capture.handle(true) == Decision::Deliver, "a new press inside the same modal must be delivered") && ok;
  return ok ? 0 : 1;
}
