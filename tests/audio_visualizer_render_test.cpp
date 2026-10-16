#include "pipewire/pipewire_service.h"
#include "pipewire/pipewire_spectrum.h"
#include "render/backend/gles_texture_manager.h"
#include "render/core/render_styles.h"
#include "render/programs/fancy_audio_visualizer_program.h"
#include "render/scene/fancy_audio_visualizer_node.h"
#include "shell/desktop/desktop_widget_factory.h"
#include "shell/desktop/desktop_widget_settings_registry.h"
#include "test_check.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
  constexpr std::array modes{FancyAudioVisualizerMode::Ribbons, FancyAudioVisualizerMode::Contour,
                             FancyAudioVisualizerMode::Bars,    FancyAudioVisualizerMode::Wave,
                             FancyAudioVisualizerMode::Mist,    FancyAudioVisualizerMode::Ornament};
  constexpr std::array names{"ribbons", "contour", "bars", "wave", "mist", "ornament"};

  void capture(const std::vector<unsigned char>& rgba, int width, int height, const std::string& name) {
    const char* dir = std::getenv("NOCTALIA_AUDIO_CAPTURE_DIR");
    if (dir == nullptr)
      return;
    std::filesystem::create_directories(dir);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const auto src = static_cast<std::size_t>(((height - y - 1) * width + x) * 4);
        pixels[static_cast<std::size_t>(y * width + x)] = (std::uint32_t{rgba[src + 3]} << 24)
            | (std::uint32_t{rgba[src]} << 16)
            | (std::uint32_t{rgba[src + 1]} << 8)
            | rgba[src + 2];
      }
    auto* surface = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(pixels.data()), CAIRO_FORMAT_ARGB32, width, height, width * 4
    );
    TEST_CHECK(
        cairo_surface_write_to_png(surface, (std::filesystem::path(dir) / (name + ".png")).c_str())
        == CAIRO_STATUS_SUCCESS
    );
    cairo_surface_destroy(surface);
  }
} // namespace

int main() {
  // Never attach to the user's Wayland/X11 display or touch the live shell.
  const EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  TEST_CHECK(display != EGL_NO_DISPLAY);
  TEST_CHECK(eglInitialize(display, nullptr, nullptr));
  TEST_CHECK(eglBindAPI(EGL_OPENGL_ES_API));
  const EGLint attrs[]{
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
  TEST_CHECK(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
  const EGLint size[]{EGL_WIDTH, 512, EGL_HEIGHT, 512, EGL_NONE};
  const auto surface = eglCreatePbufferSurface(display, config, size);
  const EGLint version[]{EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  const auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
  TEST_CHECK(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT);
  TEST_CHECK(eglMakeCurrent(display, surface, surface, context));
  {
    GlesTextureManager textures;
    FancyAudioVisualizerNode node;
    FancyAudioVisualizerProgram program;
    program.ensureInitialized();
    std::array<float, 32> spectrum{};
    std::array<float, 128> history{};
    const auto paint = [&](const FancyAudioVisualizerNode& visual, int width, int height) {
      glViewport(0, 0, width, height);
      glDisable(GL_BLEND);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      program.draw(
          visual.textureId(), static_cast<float>(width), static_cast<float>(height), static_cast<float>(width),
          static_cast<float>(height), visual.style()
      );
      std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      TEST_CHECK(glGetError() == GL_NO_ERROR);
      return pixels;
    };
    const auto render = [&](FancyAudioVisualizerMode mode, int width, int height, bool paletteProbe = false) {
      node.setAudioData(textures, spectrum, history);
      TEST_CHECK(node.textureWidth() == 128);
      FancyAudioVisualizerStyle style;
      style.mode = mode;
      style.rotationSpeed = 0.0F;
      style.cornerRadius = 0.0F;
      style.primaryColor = paletteProbe ? rgba(1.0F, 0.0F, 0.0F, 1.0F) : rgba(0.25F, 0.65F, 0.9F, 0.9F);
      style.secondaryColor = paletteProbe ? rgba(0.0F, 0.0F, 1.0F, 1.0F) : rgba(0.8F, 0.5F, 0.4F, 0.85F);
      node.setStyle(style);
      return paint(node, width, height);
    };
    for (std::size_t mode = 0; mode < modes.size(); ++mode) {
      for (const auto [width, height] : std::array{std::pair{480, 180}, std::pair{300, 300}, std::pair{180, 480}}) {
        spectrum.fill(0.0F);
        history.fill(0.0F);
        const auto quiet = render(modes[mode], width, height);
        spectrum.fill(0.65F);
        for (std::size_t i = 0; i < history.size(); ++i) {
          const float x = static_cast<float>(i) / 127.0F;
          history[i] = 0.15F + 0.65F * std::exp(-((x - 0.6F) * (x - 0.6F)) / 0.012F);
        }
        const auto loud = render(modes[mode], width, height);
        TEST_CHECK(quiet != loud); // Audio/history, not an autonomous idle animation.
        int minX = width, minY = height, maxX = -1, maxY = -1;
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>((y * width + x) * 4);
            if (loud[i + 3] > 40) {
              minX = std::min(minX, x);
              maxX = std::max(maxX, x);
              minY = std::min(minY, y);
              maxY = std::max(maxY, y);
            }
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
              TEST_CHECK(loud[i + 3] < 8);
          }
        TEST_CHECK(maxX > minX && maxY > minY);
        if (mode == 2 || mode == 3)
          TEST_CHECK(std::abs((maxX - minX) - (maxY - minY)) <= 4);
        capture(
            loud, width, height, std::string(names[mode]) + "-" + std::to_string(width) + "x" + std::to_string(height)
        );
      }
    }
    // Exercise the real desktop factory: distinct types must not all fall back to a circle.
    const auto runtime =
        std::filesystem::temp_directory_path() / ("noctalia-audio-render-" + std::to_string(::getpid()));
    std::filesystem::create_directories(runtime);
    ::setenv("PIPEWIRE_RUNTIME_DIR", runtime.c_str(), 1);
    ::setenv("PIPEWIRE_REMOTE", "noctalia-test-no-daemon", 1);
    {
      PipeWireService audio;
      PipeWireSpectrum sharedSpectrum(audio);
      DesktopWidgetFactory factory({.pipewireSpectrum = &sharedSpectrum});
      constexpr std::array types{"audio_ribbons",   "audio_contour", "audio_radial_bars",
                                 "audio_wave_ring", "audio_mist",    "audio_ornament"};
      std::array<std::vector<unsigned char>, types.size()> images;
      const auto findVisual = [](const auto& self, Node* root) -> FancyAudioVisualizerNode* {
        if (auto* visual = dynamic_cast<FancyAudioVisualizerNode*>(root))
          return visual;
        for (const auto& child : root->children())
          if (auto* visual = self(self, child.get()))
            return visual;
        return nullptr;
      };
      for (std::size_t band = 0; band < spectrum.size(); ++band)
        spectrum[band] = 0.1F + 0.6F * std::pow(std::sin(static_cast<float>(band) * 0.3F), 2.0F);
      for (std::size_t sample = 0; sample < history.size(); ++sample)
        history[sample] = sample % 19 < 5 ? 0.6F : 0.05F;
      for (std::size_t i = 0; i < types.size(); ++i) {
        auto widget = factory.create(
            types[i],
            {{"primary_color", std::string("#40a6e6")},
             {"secondary_color", std::string("#cc8066")},
             {"sensitivity", 1.3},
             {"wave_thickness", 1.0},
             {"bloom_intensity", 0.0},
             {"rotation_speed", 0.0}}
        );
        TEST_CHECK(widget != nullptr);
        widget->create();
        auto* visual = findVisual(findVisual, widget->root());
        TEST_CHECK(visual != nullptr);
        visual->setAudioData(textures, spectrum, history);
        images[i] = paint(*visual, 300, 300);
        for (std::size_t previous = 0; previous < i; ++previous)
          TEST_CHECK(images[previous] != images[i]);
        capture(images[i], 300, 300, std::string("widget-") + types[i]);
      }
    }
    std::filesystem::remove_all(runtime);
    // Mist must use history, not instantaneous spectrum, and old modes must not use history.
    spectrum.fill(0.5F);
    history.fill(0.0F);
    const auto mistEmpty = render(FancyAudioVisualizerMode::Mist, 480, 180);
    const auto bars = render(FancyAudioVisualizerMode::Bars, 300, 300);
    history.fill(0.8F);
    TEST_CHECK(mistEmpty != render(FancyAudioVisualizerMode::Mist, 480, 180));
    TEST_CHECK(bars == render(FancyAudioVisualizerMode::Bars, 300, 300));
    spectrum.fill(0.0F);
    history.fill(0.0F);
    const auto zero = render(FancyAudioVisualizerMode::Contour, 480, 180);
    spectrum.fill(std::numeric_limits<float>::quiet_NaN());
    history.fill(std::numeric_limits<float>::infinity());
    TEST_CHECK(zero == render(FancyAudioVisualizerMode::Contour, 480, 180));
    program.destroy();
    program.ensureInitialized();
    spectrum.fill(0.5F);
    TEST_CHECK(zero != render(FancyAudioVisualizerMode::Contour, 480, 180));
    // Transparent layers must not overwrite every ribbon's hue with max(RGB).
    const auto colored = render(FancyAudioVisualizerMode::Ribbons, 480, 180, true);
    bool redRibbon = false, blueRibbon = false;
    for (std::size_t i = 0; i < colored.size(); i += 4) {
      if (colored[i + 3] <= 128)
        continue;
      redRibbon |= colored[i] > colored[i + 2] + 40;
      blueRibbon |= colored[i + 2] > colored[i] + 40;
    }
    TEST_CHECK(redRibbon && blueRibbon);
    const auto specs = desktop_settings::desktopWidgetSettingSpecs("fancy_audio_visualizer");
    const auto modeSpec = std::find_if(specs.begin(), specs.end(), [](const auto& spec) {
      return spec.schema.key == "visualization_mode";
    });
    TEST_CHECK(modeSpec != specs.end());
    for (const auto* name : names) {
      TEST_CHECK(std::ranges::any_of(modeSpec->options, [name](const auto& option) { return option.value == name; }));
    }
    program.destroy();
  }
  TEST_CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
}
