#pragma once

#include "render/core/mat3.h"
#include "render/core/shader_program.h"

#include <GLES2/gl2.h>

struct JellyCatStyle;

// GLES2 port of the KISA "neko-pudding" raymarched jelly cat. Derived from
// howdeploy/kisa-stack rev 25744fa0afe9a0028e45123451b15fac36c04596; see
// licenses/howdeploy-kisa-MIT for the full upstream MIT notice.
class JellyCatProgram {
public:
  JellyCatProgram() = default;
  ~JellyCatProgram() = default;

  JellyCatProgram(const JellyCatProgram&) = delete;
  JellyCatProgram& operator=(const JellyCatProgram&) = delete;

  void ensureInitialized();
  void destroy();
  void abandon() noexcept;

  void draw(
      float surfaceWidth, float surfaceHeight, float width, float height, const JellyCatStyle& style,
      const Mat3& transform = Mat3::identity()
  ) const;

private:
  ShaderProgram m_program;
  GLint m_positionLoc = -1;
  GLint m_surfaceSizeLoc = -1;
  GLint m_quadSizeLoc = -1;
  GLint m_transformLoc = -1;
  GLint m_bodyMotionLoc = -1;
  GLint m_earMotionLoc = -1;
  GLint m_upperMotionLoc = -1;
  GLint m_milkColorLoc = -1;
  GLint m_viewportSizeLoc = -1;
  GLint m_viewAnglesLoc = -1;
};
