#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/text/cairo_text_renderer.h"
#include "shell/polkit/polkit_panel.h"
#include "test_check.h"
#include "ui/controls/button.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {
  // Exercise the production Pango shaper: line limits apply per paragraph,
  // so fixed-advance mock text cannot prove a multiline request stays readable.
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

  template <typename T> void collect(Node& node, std::vector<T*>& matches) {
    if (auto* match = dynamic_cast<T*>(&node)) {
      matches.push_back(match);
      return;
    }
    if (dynamic_cast<Input*>(&node) || dynamic_cast<Button*>(&node))
      return;
    for (const auto& child : node.children())
      collect(*child, matches);
  }

  bool inside(const Node& node, float width, float height) {
    float x = 0.0F, y = 0.0F;
    Node::absolutePosition(&node, x, y);
    return node.width() > 0.0F
        && node.height() > 0.0F
        && x >= 0.0F
        && y >= 0.0F
        && x + node.width() <= width + 0.5F
        && y + node.height() <= height + 0.5F;
  }

  void checkLayout(TextRenderer& renderer, float scale, const std::string& message) {
    // Populate only the real scene fixture: no system bus or auth request runs.
    PolkitPanel panel(nullptr, {});
    panel.setContentScale(scale);
    panel.create();
    panel.onOpen({});
    std::vector<Label*> labels;
    std::vector<Input*> inputs;
    std::vector<Button*> buttons;
    std::vector<ScrollView*> scrolls;
    collect(*panel.root(), labels);
    collect(*panel.root(), inputs);
    collect(*panel.root(), buttons);
    collect(*panel.root(), scrolls);
    TEST_CHECK(labels.size() >= 4 && inputs.size() == 1 && buttons.size() == 2 && scrolls.size() == 1);
    labels[1]->setText(message);
    labels[2]->setText("Password:");
    labels[3]->setVisible(false);
    const float width = 440.0F * scale, height = 370.0F * scale;
    panel.layout(renderer, width, height);
    TEST_CHECK(inside(*inputs[0], width, height));
    TEST_CHECK(panel.initialFocusArea() == inputs[0]->inputArea());
    for (auto* button : buttons)
      TEST_CHECK(inside(*button, width, height));
    auto& scroll = *scrolls[0];
    TEST_CHECK(scroll.scrollable());
    const float inputY = inputs[0]->y();
    scroll.setScrollOffset(scroll.maxScrollOffset());
    float messageX = 0.0F, messageY = 0.0F, scrollX = 0.0F, scrollY = 0.0F;
    Node::absolutePosition(labels[1], messageX, messageY);
    Node::absolutePosition(&scroll, scrollX, scrollY);
    const auto fullText = renderer.measureText(
        message, labels[1]->fontSize(), FontWeight::Normal, labels[1]->width(), 0, TextAlign::Start,
        Style::kumihimoEnabled() ? "Noto Sans" : "", TextEllipsize::End, false
    );
    TEST_CHECK(labels[1]->height() + 0.5F >= fullText.bottom - fullText.top);
    TEST_CHECK(messageY + labels[1]->height() <= scrollY + scroll.height() + 0.5F);
    TEST_CHECK(inputs[0]->y() == inputY && inside(*inputs[0], width, height));
    panel.onClose();
  }
} // namespace

int main() {
  TextRenderer renderer;
  std::string multiline = "Administrator approval is required.\n\nAction: Update system settings\n\n";
  for (int i = 0; i < 12; ++i)
    multiline += "Preserve all unrelated settings and permissions.\n";
  multiline += "Read the entire request before confirming.";
  for (bool material : {false, true}) {
    Style::setKumihimoEnabled(material);
    for (float scale : {1.0F, 1.5F}) {
      checkLayout(renderer, scale, multiline);
      checkLayout(renderer, scale, std::string(1600, 'W') + " final condition");
    }
  }
  Style::setKumihimoEnabled(false);
  return 0;
}
