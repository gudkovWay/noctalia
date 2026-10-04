#include "render/programs/jelly_cat_program.h"

#include "render/core/render_styles.h"

#include <array>
#include <stdexcept>

// Derived from the KISA "neko-pudding" fragment shader
// (howdeploy/kisa-stack rev 25744fa0afe9a0028e45123451b15fac36c04596).
// The raymarched SDF, deformation, normals and lighting are preserved; only the
// Qt-specific #version 440 / std140 UBO / texcoord in-out are replaced with
// GLES2 highp uniforms, a varying and gl_FragColor.
// Full upstream MIT license and copyright: licenses/howdeploy-kisa-MIT.

namespace {

  constexpr char kVertexShaderSource[] = R"(
precision highp float;

attribute vec2 a_position;
uniform vec2 u_surface_size;
uniform vec2 u_quad_size;
uniform mat3 u_transform;
varying vec2 v_uv;

void main() {
    vec2 local = a_position * u_quad_size;
    vec3 pixel = u_transform * vec3(local, 1.0);
    v_uv = a_position;
    vec2 ndc = pixel.xy / u_surface_size;
    gl_Position = vec4(ndc.x * 2.0 - 1.0, 1.0 - ndc.y * 2.0, 0.0, 1.0);
}
)";

  // The march bound (224) is a compile-time constant so the loop satisfies the
  // GLSL ES 1.00 Appendix A restrictions; no dynamic indexing or function calls
  // in loop conditions are used.
  constexpr char kFragmentShaderSource[] = R"(
precision highp float;

uniform vec4 u_body_motion;
uniform vec2 u_ear_motion;
uniform vec2 u_upper_motion;
uniform vec4 u_milk_color;
uniform vec2 u_viewport_size;
uniform vec2 u_view_angles;
varying vec2 v_uv;

float smoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float ellipsoid(vec3 p, vec3 radius) {
    // Conservative bound, including the centre of the ellipsoid.
    return (length(p / radius) - 1.0) * min(radius.x, min(radius.y, radius.z));
}

float roundedCone(vec3 p, vec3 a, vec3 b, float baseRadius, float tipRadius) {
    vec3 axis = b - a;
    float height = length(axis);
    vec3 direction = axis / height;
    float along = dot(p - a, direction);
    vec2 q = vec2(length(p - a - direction * along), along);
    // Tangent joins keep the continuous flank and rounded tip smooth.
    float taper = (baseRadius - tipRadius) / height;
    float slope = sqrt(1.0 - taper * taper);
    float region = dot(q, vec2(-taper, slope));
    if (region < 0.0) return length(q) - baseRadius;
    if (region > slope * height) return length(q - vec2(0.0, height)) - tipRadius;
    return dot(q, vec2(slope, taper)) - baseRadius;
}

float ear(vec3 p, float side) {
    return roundedCone(p, vec3(side * 0.27, 1.39, 0.30),
                       vec3(side * 0.28, 1.88, 0.29), 0.29, 0.15);
}

vec3 restPosition(vec3 p) {
    // Inverse squash: reciprocal horizontal expansion preserves volume.
    float vertical = 1.0 - u_body_motion.z;
    p.y /= vertical;
    p.xz *= sqrt(vertical);
    float h = clamp(p.y / 2.01, 0.0, 1.1);
    float bend = h * (0.4 + 0.6 * h);
    // The main mass moves first; the upper body follows with its own inertia.
    p.xz -= u_body_motion.xy * bend * 0.75 + u_upper_motion * h * h * 1.25;
    float angle = -u_body_motion.w * bend;
    p.xz = mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * p.xz;
    float earWeight = smoothstep(1.38, 2.01, p.y);
    // Positive scaling plus shear stays invertible even when the ears move
    // in opposite directions. Subtracting an x-dependent offset can fold
    // the distance field and create detached droplets.
    float earSway = (u_ear_motion.x + u_ear_motion.y) * 0.5;
    float earSpread = (u_ear_motion.y - u_ear_motion.x) * 0.9;
    p.x = (p.x - earSway * earWeight * 0.65) / exp(earSpread * earWeight);
    float earBend = mix(u_ear_motion.x, u_ear_motion.y, smoothstep(-0.28, 0.28, p.x));
    p.z -= earBend * earWeight * 0.35;
    return p;
}

float catDistance(vec3 p) {
    vec3 q = restPosition(p);
    // One gently tapered front: no separate head sphere, cheeks or neck.
    vec3 front = vec3(q.xy, (q.z - 0.28) * 1.40);
    float d = roundedCone(front, vec3(0.0, 0.12, 0.0),
                          vec3(0.0, 1.00, 0.0), 0.72, 0.64) / 1.40;
    // The rump is sheared down towards the rear and sunk below the support
    // plane, giving a spreading base rather than the underside of a ball.
    vec3 rump = q - vec3(0.0, -0.16, -0.30);
    rump.y -= 0.38 * (q.z + 0.30);
    d = smoothUnion(d, ellipsoid(rump, vec3(0.78, 1.02, 1.0)) * 0.80, 0.22);
    float paws = smoothUnion(
        ellipsoid(q - vec3(-0.32, 0.12, 0.53), vec3(0.28, 0.25, 0.32)),
        ellipsoid(q - vec3(0.32, 0.12, 0.53), vec3(0.28, 0.25, 0.32)), 0.09);
    d = smoothUnion(d, paws, 0.14);
    d = smoothUnion(d, ear(q, -1.0), 0.14);
    d = smoothUnion(d, ear(q, 1.0), 0.14);
    // The support plane never moves; there is no floating or bouncing base.
    return max(d, -p.y);
}

vec3 catNormal(vec3 p) {
    const vec2 e = vec2(0.002, -0.002);
    return normalize(e.xyy * catDistance(p + e.xyy)
                   + e.yyx * catDistance(p + e.yyx)
                   + e.yxy * catDistance(p + e.yxy)
                   + e.xxx * catDistance(p + e.xxx));
}

float faceMask(vec3 q) {
    vec2 eye = (vec2(abs(q.x), q.y) - vec2(0.17, 1.13)) / vec2(0.025, 0.037);
    vec2 nose = (q.xy - vec2(0.0, 1.02)) / vec2(0.025, 0.019);
    float ink = 1.0 - smoothstep(0.78, 1.16, min(length(eye), length(nose)));
    return ink * smoothstep(0.55, 0.67, q.z);
}

vec3 shadeCat(vec3 p, vec3 ray) {
    vec3 n = catNormal(p);
    vec3 q = restPosition(p);
    vec3 light = normalize(vec3(-0.65, 0.95, 1.4));
    vec3 view = -ray;
    vec3 halfLight = normalize(light + view);
    float facing = max(dot(n, view), 0.0);
    float diffuse = clamp((dot(n, light) + 0.42) / 1.42, 0.0, 1.0);
    float ao = 0.0;
    ao += max(0.0, 0.06 - catDistance(p + n * 0.06)) * 0.75;
    ao += max(0.0, 0.16 - catDistance(p + n * 0.16)) * 0.35;
    float occlusion = clamp(1.0 - ao * 2.2, 0.65, 1.0);
    float baseShade = mix(0.76, 1.0, smoothstep(0.02, 0.38, p.y));
    vec3 milk = pow(u_milk_color.rgb, vec3(2.2));
    vec3 color = milk * (0.38 + diffuse * 0.62) * occlusion * baseShade;
    // Warm diffuse transmission at thin ear tips; the centre stays milky.
    float thin = smoothstep(1.38, 2.01, q.y);
    float backlight = pow(max(dot(-light, view), 0.0), 3.0);
    color += milk * vec3(1.0, 0.60, 0.30) * thin * (0.055 + backlight * 0.2);
    float fresnel = 0.028 + 0.972 * pow(1.0 - facing, 5.0);
    float sheen = pow(max(dot(n, halfLight), 0.0), 38.0);
    float broad = pow(max(dot(n, halfLight), 0.0), 8.0);
    color += vec3(1.0, 0.96, 0.86) * (sheen * 0.36 + broad * 0.045 + fresnel * 0.10);
    color = mix(color, vec3(0.028, 0.018, 0.020) + sheen * 0.09, faceMask(q));
    return pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
}

void main() {
    vec2 uv = (v_uv - 0.5) * vec2(u_viewport_size.x / max(u_viewport_size.y, 1.0), -1.0) * 3.2;
    float yaw = u_view_angles.x;
    float pitch = u_view_angles.y;
    vec3 view = vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
    // Analytic camera basis stays defined when looking straight down or up.
    vec3 right = vec3(cos(yaw), 0.0, -sin(yaw));
    vec3 up = cross(view, right);
    vec3 origin = vec3(0.0, 1.0, -0.18) + view * 4.3 + right * uv.x + up * uv.y;
    vec3 ray = -view;
    float travel = 2.0;
    // ponytail: bounded artistic deformation, not FEM. Keep conservative steps;
    // a full soft-body solver is only needed for folds or changing contacts.
    for (int i = 0; i < 224; i++) {
        vec3 p = origin + ray * travel;
        float distance = catDistance(p);
        if (distance < 0.0015) {
            float alpha = u_milk_color.a;
            gl_FragColor = vec4(shadeCat(p, ray) * alpha, alpha);
            return;
        }
        travel += max(distance * 0.38, 0.0006);
        if (travel > 6.6) break;
    }
    gl_FragColor = vec4(0.0);
}
)";

} // namespace

void JellyCatProgram::ensureInitialized() {
  if (m_program.isValid()) {
    return;
  }

  m_program.create(kVertexShaderSource, kFragmentShaderSource);
  const auto id = m_program.id();

  m_positionLoc = glGetAttribLocation(id, "a_position");
  m_surfaceSizeLoc = glGetUniformLocation(id, "u_surface_size");
  m_quadSizeLoc = glGetUniformLocation(id, "u_quad_size");
  m_transformLoc = glGetUniformLocation(id, "u_transform");
  m_bodyMotionLoc = glGetUniformLocation(id, "u_body_motion");
  m_earMotionLoc = glGetUniformLocation(id, "u_ear_motion");
  m_upperMotionLoc = glGetUniformLocation(id, "u_upper_motion");
  m_milkColorLoc = glGetUniformLocation(id, "u_milk_color");
  m_viewportSizeLoc = glGetUniformLocation(id, "u_viewport_size");
  m_viewAnglesLoc = glGetUniformLocation(id, "u_view_angles");

  if (m_positionLoc < 0
      || m_surfaceSizeLoc < 0
      || m_quadSizeLoc < 0
      || m_transformLoc < 0
      || m_bodyMotionLoc < 0
      || m_earMotionLoc < 0
      || m_upperMotionLoc < 0
      || m_milkColorLoc < 0
      || m_viewportSizeLoc < 0
      || m_viewAnglesLoc < 0) {
    throw std::runtime_error("failed to query jelly cat shader locations");
  }
}

void JellyCatProgram::destroy() {
  m_program.destroy();
  m_positionLoc = -1;
  m_surfaceSizeLoc = -1;
  m_quadSizeLoc = -1;
  m_transformLoc = -1;
  m_bodyMotionLoc = -1;
  m_earMotionLoc = -1;
  m_upperMotionLoc = -1;
  m_milkColorLoc = -1;
  m_viewportSizeLoc = -1;
  m_viewAnglesLoc = -1;
}

void JellyCatProgram::abandon() noexcept { m_program.abandon(); }

void JellyCatProgram::draw(
    float surfaceWidth, float surfaceHeight, float width, float height, const JellyCatStyle& style,
    const Mat3& transform
) const {
  if (!m_program.isValid() || width <= 0.0F || height <= 0.0F) {
    return;
  }

  static constexpr std::array<GLfloat, 12> kQuad = {
      0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
  };

  glUseProgram(m_program.id());
  glUniform2f(m_surfaceSizeLoc, surfaceWidth, surfaceHeight);
  glUniform2f(m_quadSizeLoc, width, height);
  glUniformMatrix3fv(m_transformLoc, 1, GL_FALSE, transform.m.data());
  glUniform4f(
      m_bodyMotionLoc, style.bodyMotion[0], style.bodyMotion[1], style.bodyMotion[2], style.bodyMotion[3]
  );
  glUniform2f(m_earMotionLoc, style.earMotion[0], style.earMotion[1]);
  glUniform2f(m_upperMotionLoc, style.upperMotion[0], style.upperMotion[1]);
  glUniform4f(
      m_milkColorLoc, style.milkColor.r, style.milkColor.g, style.milkColor.b, style.milkColor.a
  );
  glUniform2f(m_viewportSizeLoc, width, height);
  glUniform2f(m_viewAnglesLoc, style.yaw, style.pitch);

  const auto posAttr = static_cast<GLuint>(m_positionLoc);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, kQuad.data());
  glEnableVertexAttribArray(posAttr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(posAttr);
}
