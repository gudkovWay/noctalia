#pragma once

#include <string>
#include <string_view>

namespace noctalia::instance {
  enum class Role { Legacy, Primary, Secondary };

  struct Context {
    Role role = Role::Legacy;
    std::string id;
    std::string waylandSocket;
    std::string runtimeDir;
    std::string legacyDisplay;

    [[nodiscard]] bool isolated() const noexcept { return role != Role::Legacy; }
    [[nodiscard]] bool ownsGlobalServices() const noexcept { return role != Role::Secondary; }
    [[nodiscard]] std::string_view roleName() const noexcept;
    [[nodiscard]] std::string socketPath(std::string_view prefix, std::string_view suffix) const;
    [[nodiscard]] std::string lockPath() const;
    [[nodiscard]] std::string statePath(std::string_view base) const;
  };

  // Pure resolution: no socket connection, file creation or environment mutation.
  // Canonical parent aliases and absolute displays refer to the same identity.
  [[nodiscard]] Context resolve(std::string_view role, std::string_view display, std::string_view runtime);
  // Immutable after first use; root CLI options must be consumed before this call.
  [[nodiscard]] const Context& current();
  // Consumes only leading root --session-role <role>; CLI overrides environment.
  void consumeOptions(int& argc, char** argv);
  // Process-local context inherited by plugin children. Existing HOME/XDG/base
  // root overrides are never changed. Outputs are not accepted as identity input.
  void exportSubprocessContext(std::string_view stateDir);

  // A native exit is safe only when its socket is served by the selected Wayland
  // compositor process (same SO_PEERCRED PID and UID), not an inherited env hint.
  [[nodiscard]] bool socketBelongsToWayland(std::string_view socketPath, std::string_view waylandSocket);
} // namespace noctalia::instance
