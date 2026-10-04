#pragma once

#include "render/core/render_styles.h"
#include "render/scene/node.h"

// Scene node for the procedural jelly cat. Holds only the render-facing style;
// spring simulation and pointer/audio input live in the higher-level control.
class JellyCatNode : public Node {
public:
  JellyCatNode() : Node(NodeType::JellyCat) {}

  [[nodiscard]] const JellyCatStyle& style() const noexcept { return m_style; }

  void setStyle(const JellyCatStyle& style) {
    if (m_style == style) {
      return;
    }
    m_style = style;
    markPaintDirty();
  }

private:
  JellyCatStyle m_style;
};
