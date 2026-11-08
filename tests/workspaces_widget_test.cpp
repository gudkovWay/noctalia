#include "shell/bar/widgets/workspaces_widget.h"
#include "tests/test_check.h"

#include <string>
#include <utility>

class WorkspacesWidgetTestAccess {
public:
  static std::string tooltip(std::string name, const std::string& label, bool showLabel) {
    return WorkspacesWidget::workspaceTooltipText(Workspace{.name = std::move(name)}, label, showLabel);
  }
  static std::optional<std::size_t> image(const Workspace& workspace, std::size_t count) {
    return WorkspacesWidget::cardImageIndex(workspace, count);
  }
};

int main() {
  // A name cut short by max_label_chars is recoverable on hover.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("client portal", "cli", true) == "client portal");
  // A pill that already spells out the whole name has nothing to add.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("web", "web", true).empty());
  // label_source = "id" labels a named workspace by number, so the name is only reachable on hover.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("client portal", "3", true) == "client portal");
  // A hidden label shows nothing, even when its text would have matched the name.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("web", "web", false) == "web");
  // Unnamed workspaces have no tooltip, never an empty one.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("", "3", true).empty());
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("", "", false).empty());
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "1"}, 5) == 0);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "11"}, 5) == 0);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "2"}, 5) == 1);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "12"}, 5) == 1);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "3"}, 5) == 2);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "13"}, 5) == 2);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "4"}, 5) == 3);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "14"}, 5) == 3);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "5"}, 5) == 4);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "15"}, 5) == 4);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "20"}, 5) == 4);
  TEST_CHECK(WorkspacesWidgetTestAccess::image(Workspace{.id = "named:web", .index = 2}, 5) == 1);
  TEST_CHECK(!WorkspacesWidgetTestAccess::image(Workspace{.id = "named:web"}, 5));
  TEST_CHECK(!WorkspacesWidgetTestAccess::image(Workspace{.id = "1"}, 0));
  for (const char* id : {"2", "3"}) {
    const auto inactive = WorkspacesWidgetTestAccess::image(Workspace{.id = id, .active = false}, 5);
    const auto active = WorkspacesWidgetTestAccess::image(Workspace{.id = id, .active = true}, 5);
    TEST_CHECK(inactive == active);
  }

  return 0;
}
