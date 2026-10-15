#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/text/cairo_text_renderer.h"
#include "shell/control_center/tabs/home_tab.h"
#include "test_check.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/style.h"

#include <cstdlib>
#include <vector>

namespace {
  class TextRenderer final : public Renderer {
  public:
    TextRenderer() { m_text.initialize(nullptr, nullptr); }
    TextMetrics measureText(
        std::string_view text, float size, FontWeight weight, float width, int lines, TextAlign align,
        std::string_view family, TextEllipsize ellipsize, bool markup
    ) override {
      return convert(m_text.measure(1.0F, text, size, weight, width, lines, align, family, ellipsize, markup));
    }
    TextMetrics measureFont(float size, FontWeight weight) override {
      return convert(m_text.measureFont(1.0F, size, weight));
    }
    void measureTextCursorStops(
        std::string_view text, float size, const std::vector<std::size_t>& offsets, std::vector<float>& stops,
        FontWeight weight
    ) override {
      m_text.measureCursorStops(1.0F, text, size, offsets, stops, weight);
    }
    void measureTextCursorStopsWrapped(
        std::string_view text, float size, const std::vector<std::size_t>& offsets, float width,
        std::vector<TextCursorStop>& stops, FontWeight weight
    ) override {
      m_text.measureCursorStopsWrapped(1.0F, text, size, offsets, width, stops, weight);
    }
    TextMetrics measureGlyph(char32_t, float size) override { return TextMetrics{.width = size, .bottom = size}; }
    TextureManager& textureManager() override { std::abort(); }
    float renderScale() const noexcept override { return 1.0F; }

  private:
    static TextMetrics convert(const CairoTextRenderer::TextMetrics& m) {
      return TextMetrics{
          .width = m.width,
          .left = m.left,
          .right = m.right,
          .top = m.top,
          .bottom = m.bottom,
          .inkTop = m.inkTop,
          .inkBottom = m.inkBottom,
          .inkLeft = m.inkLeft,
          .inkRight = m.inkRight,
          .capHeight = m.capHeight,
          .lineCount = m.lineCount
      };
    }
    CairoTextRenderer m_text;
  };

  void labelsIn(Node& node, std::vector<Label*>& labels) {
    if (auto* label = dynamic_cast<Label*>(&node)) {
      labels.push_back(label);
      return;
    }
    for (const auto& child : node.children())
      labelsIn(*child, labels);
  }

  void checkProfile(TextRenderer& renderer, bool material, float scale) {
    Style::setKumihimoEnabled(material);
    HomeTab tab(ControlCenterServices{});
    tab.setContentScale(scale);
    auto scene = tab.create();
    auto* profile = dynamic_cast<Flex*>(scene->children().front().get());
    TEST_CHECK(profile != nullptr);
    std::vector<Label*> labels;
    labelsIn(*profile, labels);
    TEST_CHECK(labels.size() == 4);
    labels[0]->setText("Profile");
    labels[1]->setText("profile@desktop");
    labels[2]->setText("Uptime - 4 hours 28 minutes");
    labels[3]->setText("Noctalia v5.2.1 (native build)");
    for (int pass = 0; pass < 2; ++pass) {
      tab.layout(renderer, 660.0F * scale, 420.0F * scale);
      float cardX = 0.0F, cardY = 0.0F;
      Node::absolutePosition(profile, cardX, cardY);
      for (const auto* label : labels) {
        float x = 0.0F, y = 0.0F;
        Node::absolutePosition(label, x, y);
        TEST_CHECK(y >= cardY + profile->paddingTop() - 0.5F);
        TEST_CHECK(y + label->height() <= cardY + profile->height() - profile->paddingBottom() + 0.5F);
      }
      const auto* nextRow = scene->children()[1].get();
      TEST_CHECK(profile->y() + profile->height() + scene->gap() <= nextRow->y() + 0.5F);
    }
    tab.onClose();
  }
} // namespace

int main() {
  TextRenderer renderer;
  for (bool material : {false, true}) {
    for (float scale : {1.0F, 1.25F, 1.5F})
      checkProfile(renderer, material, scale);
  }
  Style::setKumihimoEnabled(false);
  return 0;
}
