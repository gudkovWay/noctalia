#include "shell/panel/hover_panel_state.h"

#include <print>

int main() {
  HoverPanelState state;
  int failures = 0;
  const auto check = [&](bool value, const char* reason) {
    if (!value) {
      ++failures;
      std::println(stderr, "hover_panel: {}", reason);
    }
  };
  using Kind = HoverPanelState::Kind;
  state.trigger(1, true, 0);
  check(state.advance(119).kind == Kind::None, "brief hover opened a wing");
  auto action = state.advance(120);
  check(action.kind == Kind::Open && action.key == 1, "dwell did not open the requested wing");
  check(state.advance(1000).kind == Kind::None, "held trigger reopened its wing");
  state.trigger(1, false, 1000);
  state.panel(true, 1100);
  check(state.advance(1500).kind == Kind::None, "moving from trigger into wing closed it");
  state.panel(false, 1500);
  check(state.advance(1719).kind == Kind::None, "union leave closed before grace elapsed");
  state.trigger(1, true, 1719);
  check(state.advance(2000).kind == Kind::None && state.activeKey() == 1, "reentry failed to cancel close");
  state.trigger(1, false, 2000);
  action = state.advance(2220);
  check(action.kind == Kind::Close && action.key == 1 && state.activeKey() == 0, "union leave did not close wing");

  state.trigger(2, true, 3000);
  state.trigger(2, false, 3050);
  check(state.advance(3200).kind == Kind::None, "cancelled pending trigger opened later");
  state.trigger(1, true, 4000);
  check(state.advance(4120).kind == Kind::Open, "tray could not reopen");
  state.trigger(1, false, 4200);
  state.trigger(2, true, 4200);
  check(state.advance(4319).kind == Kind::None, "switch bypassed dwell");
  action = state.advance(4320);
  check(action.kind == Kind::Open && action.key == 2 && state.activeKey() == 2, "resource switch retained tray owner");
  check(state.advance(5000).kind == Kind::None, "old tray deadline closed resources");

  state.trigger(2, false, 5000);
  state.panel(true, 5050);
  state.popup(true, 5100);
  state.panel(false, 5150);
  check(state.advance(6000).kind == Kind::None, "native context menu failed to hold wing");
  state.popup(false, 6000);
  check(state.advance(6219).kind == Kind::None, "menu close bypassed leave grace");
  check(state.advance(6220).kind == Kind::Close, "wing remained after menu and pointer left");
  state.trigger(1, true, 7000);
  state.reset();
  check(
      !state.delay(8000).has_value() && state.advance(8000).kind == Kind::None,
      "external close/reload retained a pending reveal"
  );
  return failures == 0 ? 0 : 1;
}
