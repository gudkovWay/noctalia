#include "app/instance_context.h"
#include "app/single_instance_lock.h"
#include "config/config_service.h"
#include "core/deferred_call.h"
#include "core/process/process.h"
#include "hooks/hook_manager.h"
#include "ipc/ipc_client.h"
#include "ipc/ipc_service.h"
#include "ipc/unix_socket_address.h"
#include "launcher/dmenu_ipc.h"
#include "launcher/dmenu_socket_path.h"
#include "notification/notification_manager.h"
#include "scripting/luau_host.h"
#include "scripting/script_api_context.h"
#include "tests/test_check.h"
#include "theme/custom_palettes.h"
#include "theme/palette.h"
#include "theme/template_apply_service.h"
#include "util/file_utils.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
  void child(const std::function<void()>& check) {
    const pid_t pid = ::fork();
    TEST_CHECK(pid >= 0);
    if (pid == 0) {
      check();
      _exit(0);
    }
    int status = 0;
    TEST_CHECK(::waitpid(pid, &status, 0) == pid);
    TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  void rejects(std::string_view role, std::string_view display, std::string_view runtime) {
    bool rejected = false;
    try {
      (void)noctalia::instance::resolve(role, display, runtime);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    TEST_CHECK(rejected);
  }
} // namespace

int main(int argc, char** argv) {
  // Test-only CLI adapter: exercises the real native client from a child exec,
  // without initializing another fixture or any shell/Wayland surface.
  if (argc == 3 && std::string_view(argv[1]) == "msg" && std::string_view(argv[2]) == "status") {
    return IpcClient::send("status");
  }
  const auto root = std::filesystem::temp_directory_path() / ("nci-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "one");
  std::filesystem::create_directories(root / "two");
  std::filesystem::create_directory_symlink(root / "one", root / "alias");
  const auto a = noctalia::instance::resolve("primary", "wayland-1", (root / "one").string());
  const auto same = noctalia::instance::resolve("secondary", (root / "alias/wayland-1").string(), root.string());
  const auto b = noctalia::instance::resolve("secondary", "wayland-1", (root / "two").string());
  TEST_CHECK(a.id == same.id);
  TEST_CHECK(noctalia::instance::resolve("secondary", (root / "one/wayland-1").string(), "").id == a.id);
  TEST_CHECK(a.socketPath("noctalia", ".sock") == same.socketPath("noctalia", ".sock"));
  TEST_CHECK(a.lockPath() == same.lockPath());
  TEST_CHECK(a.id != b.id);
  TEST_CHECK(a.id.size() == 64);
  TEST_CHECK(a.waylandSocket == (root / "one/wayland-1").string());
  const auto longDir = root / std::string(70, 'x');
  std::filesystem::create_directory(longDir);
  const auto longContext = noctalia::instance::resolve("secondary", (longDir / "wayland-1").string(), "");
  TEST_CHECK(longContext.socketPath("noctalia-dmenu", ".sock").size() < sizeof(sockaddr_un::sun_path));
  TEST_CHECK(longContext.id != a.id);
  TEST_CHECK(!noctalia::ipc::unixSocketPeerIsCurrentUser(-1));
  rejects("primary", "", root.string());
  rejects("secondary", "wayland-1", "");
  rejects("secondary", "wayland-1", "relative");
  rejects("secondary", "../escape", root.string());
  rejects("invalid", "wayland-1", root.string());
  rejects("secondary", ".", root.string());
  child([&]() {
    ::setenv("NOCTALIA_SESSION_ROLE", "secondary", 1);
    ::setenv("WAYLAND_SOCKET", "3", 1);
    bool refused = false;
    try {
      (void)noctalia::instance::current();
    } catch (const std::runtime_error&) {
      refused = true;
    }
    TEST_CHECK(refused);
  });
  child([&]() {
    ::setenv("NOCTALIA_SESSION_ROLE", "invalid", 1);
    char binary[] = "noctalia", flag[] = "--session-role", role[] = "secondary", msg[] = "msg", status[] = "status";
    char* args[]{binary, flag, role, msg, status, nullptr};
    int argc = 5;
    noctalia::instance::consumeOptions(argc, args);
    TEST_CHECK(argc == 3 && std::string(args[1]) == "msg" && std::string(args[2]) == "status");
    TEST_CHECK(std::string(std::getenv("NOCTALIA_SESSION_ROLE")) == "secondary");
  });

  ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
  ::setenv("NOCTALIA_STATE_HOME", (root / "state").c_str(), 1);
  ::setenv("NOCTALIA_DATA_HOME", (root / "data").c_str(), 1);
  ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
  ::unsetenv("WAYLAND_SOCKET");
  std::filesystem::create_directories(root / "config/noctalia");
  {
    std::ofstream f(root / "config/noctalia/config.toml");
    f
        << "[dock]\nenabled = true\n[theme.templates]\nenable_builtin_templates = false\n"
        << "[theme.templates.user.multiwm-proof]\nenabled = true\ninput_path = \""
        << (root / "input.tmpl").string()
        << "\"\noutput_path = [\""
        << (root / "template.out").string()
        << "\"]\n";
  }
  {
    std::ofstream f(root / "input.tmpl");
    f << "native template side effect\n";
  }
  {
    std::filesystem::create_directories(root / "state/noctalia");
    std::ofstream f(root / "state/noctalia/settings.toml");
    f << "[dock]\nenabled = false\n";
  }

  for (const std::string display : {"one/wayland-1", "two/wayland-1"}) {
    child([&]() {
      ::setenv("NOCTALIA_SESSION_ROLE", "secondary", 1);
      ::setenv("WAYLAND_DISPLAY", display.c_str(), 1);
      const auto& context = noctalia::instance::current();
      TEST_CHECK(FileUtils::stateDir() == (root / "state/noctalia/sessions" / context.id).string());
      TEST_CHECK(FileUtils::configDir() == (root / "config/noctalia").string());
      TEST_CHECK(FileUtils::dataDir() == (root / "data/noctalia").string());
      noctalia::instance::exportSubprocessContext(FileUtils::stateDir());
      ConfigService config;
      TEST_CHECK(config.config().dock.enabled); // shared legacy settings are NOT copied/loaded
      config.setDockEnabled(false);
      TEST_CHECK(!config.config().dock.enabled);
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "settings.toml"));
      TEST_CHECK(!config.stateString("instance", "receipt").has_value());
      TEST_CHECK(config.setStateString("instance", "receipt", context.id));
      TEST_CHECK(config.stateString("instance", "receipt") == context.id);
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "state.toml"));
      NotificationManager notifications;
      notifications.loadPersistedHistory();
      TEST_CHECK(notifications.history().empty());
      NotificationRequest notification;
      notification.appName = "instance-fixture";
      notification.summary = context.id;
      notification.timeout = 0;
      notifications.addOrReplace(std::move(notification));
      notifications.flushPersistedHistory();
      TEST_CHECK(notifications.history().size() == 1);
      TEST_CHECK(notifications.history().front().notification.summary == context.id);
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "notification_history.json"));
      const auto plugin = FileUtils::pluginDataDir("test/plugin");
      scripting::ScriptApiContext api;
      LuauHost host(api, "test/plugin:service");
      TEST_CHECK(host.exec("=instance-storage", R"(
        local dir = noctalia.pluginDataDir()
        assert(dir == noctalia.getenv("NOCTALIA_SESSION_STATE_DIR") .. "/plugins/data/test/plugin")
        assert(noctalia.writeFile(dir .. "/receipt", noctalia.getenv("NOCTALIA_INSTANCE_ID")))
      )"));
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(plugin) / "receipt"));
      const auto processResult = process::runSync(
          std::vector<std::string>{
              "/bin/sh", "-c",
              "test -x \"$NOCTALIA_BINARY\" && test \"$WAYLAND_DISPLAY\" = \"$NOCTALIA_WAYLAND_SOCKET\" && "
              "test \"$NOCTALIA_STATE_HOME\" != \"$NOCTALIA_SESSION_STATE_DIR\" && "
              "mkdir \"$NOCTALIA_SESSION_STATE_DIR/child\" && : > \"$NOCTALIA_SESSION_STATE_DIR/child/marker\""
          }
      );
      TEST_CHECK(processResult.exitCode == 0);
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "child/marker"));
      noctalia::theme::GeneratedPalette palette;
      palette.dark["primary"] = 0x112233;
      TEST_CHECK(noctalia::theme::saveCustomPaletteFromGenerated("local-proof", palette, nullptr));
      TEST_CHECK(std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "palettes/local-proof.json"));
      TEST_CHECK(!std::filesystem::exists(root / "config/noctalia/palettes/local-proof.json"));
      TEST_CHECK(!config.config().theme.templates.userTemplates.empty());
      int applications = 0;
      {
        noctalia::theme::TemplateApplyService templates(config);
        templates.setAfterApplyCallback([&](std::string_view, bool) { ++applications; });
        templates.apply(palette, "dark", true, true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        while (std::chrono::steady_clock::now() < deadline) {
          for (auto& call : DeferredCall::takePending())
            call();
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        IpcService templateIpc;
        templates.registerIpc(templateIpc);
        TEST_CHECK(templateIpc.execute("templates-apply").starts_with("error: secondary instances"));
      }
      TEST_CHECK(applications == 0);
      TEST_CHECK(!std::filesystem::exists(root / "template.out"));
      HooksConfig hooksConfig;
      hooksConfig.commands[static_cast<std::size_t>(HookKind::Started)] = {"global-start"};
      hooksConfig.commands[static_cast<std::size_t>(HookKind::ColorsChanged)] = {"global-theme"};
      hooksConfig.commands[static_cast<std::size_t>(HookKind::SessionUnlocked)] = {"display-wake"};
      HookManager hooks;
      hooks.reload(hooksConfig);
      std::vector<std::string> effects;
      hooks.setCommandRunner([&](const std::string& command) {
        effects.push_back(command);
        return true;
      });
      hooks.fire(HookKind::Started);
      hooks.fire(HookKind::ColorsChanged);
      hooks.fire(HookKind::SessionUnlocked);
      TEST_CHECK(effects == std::vector<std::string>{"display-wake"});
      SingleInstanceLock lock;
      TEST_CHECK(lock.tryAcquire());
      TEST_CHECK(lock.path() == context.lockPath());
      const auto compatPath = context.runtimeDir
          + "/noctalia-"
          + std::filesystem::path(context.waylandSocket).filename().string()
          + ".lock";
      const int legacyFd = ::open(compatPath.c_str(), O_RDWR | O_CLOEXEC);
      TEST_CHECK(legacyFd >= 0);
      TEST_CHECK(::flock(legacyFd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK);
      ::close(legacyFd);
      SingleInstanceLock duplicate;
      TEST_CHECK(!duplicate.tryAcquire());
      IpcService ipc;
      TEST_CHECK(ipc.start());
      TEST_CHECK(ipc.socketPath() == context.socketPath("noctalia", ".sock"));
      TEST_CHECK(noctalia::launcher::resolveDmenuSocketPath().path == context.socketPath("noctalia-dmenu", ".sock"));
      DmenuIpcService dmenu;
      TEST_CHECK(dmenu.start());
      const auto dmenuAddress = noctalia::ipc::unixSocketAddress(noctalia::launcher::resolveDmenuSocketPath().path);
      TEST_CHECK(dmenuAddress.has_value());
      const int dmenuClient = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      TEST_CHECK(dmenuClient >= 0);
      TEST_CHECK(
          ::connect(dmenuClient, reinterpret_cast<const sockaddr*>(&dmenuAddress->address), dmenuAddress->size) == 0
      );
      ::close(dmenuClient);
      bool dispatched = false;
      ipc.bind(noctalia::cli::msg::status, [&](const std::string&) {
        dispatched = true;
        return "ok\n";
      });
      std::thread server([&]() {
        pollfd ready{ipc.listenFd(), POLLIN, 0};
        TEST_CHECK(::poll(&ready, 1, 2000) > 0);
        ipc.dispatch();
      });
      TEST_CHECK(IpcClient::send("status") == 0);
      server.join();
      TEST_CHECK(dispatched);
      const std::string ownBinary = std::getenv("NOCTALIA_BINARY");
      const auto ownBin = std::filesystem::path(FileUtils::stateDir()) / "current-bin";
      const auto otherBin = std::filesystem::path(FileUtils::stateDir()) / "upstream-bin";
      std::filesystem::create_directories(ownBin);
      std::filesystem::create_directories(otherBin);
      std::filesystem::create_symlink(ownBinary, ownBin / "noctalia");
      {
        std::ofstream other(otherBin / "noctalia");
        other << "#!/bin/sh\n: > \"$NOCTALIA_SESSION_STATE_DIR/wrong-client\"\n";
      }
      std::filesystem::permissions(otherBin / "noctalia", std::filesystem::perms::owner_all);
      const char* oldPath = std::getenv("PATH");
      const std::optional<std::string> savedPath =
          oldPath != nullptr ? std::optional<std::string>{oldPath} : std::nullopt;
      ::setenv("PATH", otherBin.c_str(), 1);
      ::setenv("NOCTALIA_BINARY", (ownBin / "noctalia").c_str(), 1);
      auto dispatchChild = [&]() {
        pollfd ready{ipc.listenFd(), POLLIN, 0};
        TEST_CHECK(::poll(&ready, 1, 2000) > 0);
        ipc.dispatch();
      };
      dispatched = false;
      std::thread capturedServer(dispatchChild);
      process::RunOptions options;
      options.env.push_back({"PATH", otherBin.string()});
      const auto captured = process::runSync(std::vector<std::string>{"/bin/sh", "-c", "noctalia msg status"}, options);
      capturedServer.join();
      TEST_CHECK(captured.exitCode == 0 && dispatched);
      TEST_CHECK(!std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "wrong-client"));
      TEST_CHECK(std::string(std::getenv("PATH")) == otherBin.string()); // parent PATH is unchanged
      dispatched = false;
      std::thread detachedServer(dispatchChild);
      TEST_CHECK(process::runAsync(std::vector<std::string>{"/bin/sh", "-c", "noctalia msg status"}));
      detachedServer.join();
      TEST_CHECK(dispatched);
      TEST_CHECK(!std::filesystem::exists(std::filesystem::path(FileUtils::stateDir()) / "wrong-client"));
      if (savedPath)
        ::setenv("PATH", savedPath->c_str(), 1);
      else
        ::unsetenv("PATH");
      noctalia::instance::exportSubprocessContext(FileUtils::stateDir());
      TEST_CHECK(std::string(std::getenv("NOCTALIA_SESSION_STATE_DIR")) == FileUtils::stateDir());
      TEST_CHECK(std::string(std::getenv("NOCTALIA_IPC_SOCKET")) == ipc.socketPath());
      TEST_CHECK(std::string(std::getenv("NOCTALIA_STATE_HOME")) == (root / "state").string());
    });
  }

  // Two independent instance contexts hold locks and native IPC concurrently,
  // despite identical display basenames. No Wayland/GUI/user bus is involved.
  struct Child {
    pid_t pid = -1;
    int ready[2]{};
    int release[2]{};
  };
  std::array<Child, 2> children;
  for (std::size_t i = 0; i < children.size(); ++i) {
    auto& process = children[i];
    TEST_CHECK(::pipe(process.ready) == 0 && ::pipe(process.release) == 0);
    process.pid = ::fork();
    TEST_CHECK(process.pid >= 0);
    if (process.pid == 0) {
      ::close(process.ready[0]);
      ::close(process.release[1]);
      ::setenv("NOCTALIA_SESSION_ROLE", "secondary", 1);
      const std::string display = i == 0 ? "one/simultaneous" : "two/simultaneous";
      ::setenv("WAYLAND_DISPLAY", display.c_str(), 1);
      SingleInstanceLock lock;
      TEST_CHECK(lock.tryAcquire());
      IpcService ipc;
      TEST_CHECK(ipc.start());
      TEST_CHECK(::write(process.ready[1], "r", 1) == 1);
      char release = 0;
      TEST_CHECK(::read(process.release[0], &release, 1) == 1);
      _exit(0);
    }
    ::close(process.ready[1]);
    ::close(process.release[0]);
  }
  for (auto& process : children) {
    char ready = 0;
    TEST_CHECK(::read(process.ready[0], &ready, 1) == 1);
    ::close(process.ready[0]);
  }
  for (auto& process : children) {
    TEST_CHECK(::write(process.release[1], "x", 1) == 1);
    ::close(process.release[1]);
    int status = 0;
    TEST_CHECK(::waitpid(process.pid, &status, 0) == process.pid);
    TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  TEST_CHECK(!std::filesystem::exists(root / "state/noctalia/state.toml"));
  TEST_CHECK(!std::filesystem::exists(root / "state/noctalia/notification_history.json"));
  child([&]() {
    ::unsetenv("NOCTALIA_SESSION_ROLE");
    ::unsetenv("WAYLAND_DISPLAY");
    ::setenv("NOCTALIA_INSTANCE_ID", "stale-parent", 1);
    ::setenv("NOCTALIA_SESSION_STATE_DIR", "/stale-parent", 1);
    noctalia::instance::exportSubprocessContext(FileUtils::stateDir());
    TEST_CHECK(std::getenv("NOCTALIA_INSTANCE_ID") == nullptr);
    TEST_CHECK(std::getenv("NOCTALIA_SESSION_STATE_DIR") == nullptr);
    TEST_CHECK(std::string(std::getenv("NOCTALIA_STATE_HOME")) == (root / "state").string());
    TEST_CHECK(FileUtils::stateDir() == (root / "state/noctalia").string());
    ConfigService config;
    TEST_CHECK(!config.config().dock.enabled);
    IpcService ipc;
    TEST_CHECK(ipc.start());
    TEST_CHECK(ipc.socketPath() == (root / "noctalia-wayland-0.sock").string());
  });
  for (const std::string display : {"one/wayland-1", "two/wayland-1"}) {
    const auto c = noctalia::instance::resolve("secondary", display, root.string());
    TEST_CHECK(std::filesystem::exists(root / "state/noctalia/sessions" / c.id / "plugins/data/test/plugin/receipt"));
  }
  std::filesystem::remove_all(root);
}
