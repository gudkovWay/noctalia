#include "render/backend/gles_texture_manager.h"
#include "render/programs/image_program.h"
#include "test_check.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <vector>

int main() {
  const auto display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  TEST_CHECK(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr));
  TEST_CHECK(eglBindAPI(EGL_OPENGL_ES_API));
  const EGLint attributes[]{
      EGL_SURFACE_TYPE,
      EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_NONE
  };
  EGLConfig config{};
  EGLint count = 0;
  TEST_CHECK(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
  const EGLint size[]{EGL_WIDTH, 100, EGL_HEIGHT, 100, EGL_NONE};
  const auto surface = eglCreatePbufferSurface(display, config, size);
  const EGLint version[]{EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  const auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
  TEST_CHECK(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT);
  TEST_CHECK(eglMakeCurrent(display, surface, surface, context));
  {
    GlesTextureManager textures;
    std::vector<unsigned char> cloth(100 * 100 * 4, 0);
    for (int y = 0; y < 80; ++y)
      for (int x = 0; x < 100; ++x) {
        const auto index = static_cast<std::size_t>((y * 100 + x) * 4);
        cloth[index] = static_cast<unsigned char>(y + 80);
        cloth[index + 3] = 255;
      }
    auto texture = textures.loadFromPixels(cloth.data(), 100, 100, TextureDataFormat::Rgba, TextureFilter::Nearest);
    ImageProgram program;
    program.ensureInitialized();
    const auto render = [&](const ImageHem& hem) {
      glViewport(0, 0, 100, 100);
      glDisable(GL_BLEND);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      program.draw(
          texture.id, 100, 100, 100, 100, rgba(1, 1, 1, 1), false, false, 1, 0, rgba(0, 0, 0, 0), 0, 0, 100, 100,
          Mat3::identity(), {}, hem
      );
      std::vector<unsigned char> pixels(100 * 100 * 4);
      glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      TEST_CHECK(glGetError() == GL_NO_ERROR);
      return pixels;
    };
    const auto pixel = [](const auto& pixels, int x, int y, int channel) {
      return pixels[static_cast<std::size_t>(((99 - y) * 100 + x) * 4 + channel)];
    };
    const auto quiet = render({});
    ImageHem hem;
    hem.enabled = true;
    hem.offsets.fill(0.08F);
    const auto loud = render(hem);
    TEST_CHECK(quiet != loud);
    TEST_CHECK(pixel(quiet, 50, 77, 3) > 240 && pixel(loud, 50, 77, 3) < 8);
    TEST_CHECK(pixel(quiet, 50, 20, 0) == pixel(loud, 50, 20, 0));
    TEST_CHECK(pixel(loud, 50, 90, 3) < 8);
    TEST_CHECK(quiet == render({}));
    program.destroy();
    textures.unload(texture);
  }
  TEST_CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
}
