#include "test_check.h"
#include "ui/visuals/audio_visualizer_signal.h"

#include <array>
#include <cmath>
#include <limits>

int main() {
  AudioVisualizerSignal signal;
  std::array<float, 32> input{};
  input.fill(0.8F);
  signal.accept(input);
  signal.step(50.0F, true);
  TEST_CHECK(signal.spectrum()[0] > 0.0F && signal.spectrum()[0] < 0.8F);
  TEST_CHECK(signal.history().back() > 0.0F);
  const auto history = signal.history();
  signal.step(0.0F, true);
  TEST_CHECK(signal.history() == history);
  for (int i = 0; i < 20; ++i)
    signal.step(50.0F, true);
  TEST_CHECK(std::abs(signal.spectrum()[0] - 0.8F) < 0.001F);
  input.fill(0.0F);
  signal.accept(input);
  signal.step(50.0F, true);
  TEST_CHECK(signal.history()[126] > signal.history()[127]);
  TEST_CHECK(signal.spectrum()[0] > 0.0F);
  signal.step(50.0F, false);
  for (float value : signal.spectrum())
    TEST_CHECK(value == 0.0F);
  for (float value : signal.history())
    TEST_CHECK(value == 0.0F);
  input.fill(std::numeric_limits<float>::quiet_NaN());
  input[1] = std::numeric_limits<float>::infinity();
  input[2] = -4.0F;
  input[3] = 4.0F;
  signal.accept(input);
  signal.step(50.0F, true);
  TEST_CHECK(signal.spectrum()[0] == 0.0F && signal.spectrum()[1] == 0.0F);
  TEST_CHECK(signal.spectrum()[2] == 0.0F && signal.spectrum()[3] <= 1.0F);
  signal.step(std::numeric_limits<float>::quiet_NaN(), true);
  for (float value : signal.history())
    TEST_CHECK(std::isfinite(value));
  signal.accept({});
  for (int i = 0; i < 128; ++i)
    signal.step(50.0F, true);
  for (float value : signal.spectrum())
    TEST_CHECK(value == 0.0F);
  // The oldest sample is the beginning of the release, not the final silence.
  TEST_CHECK(signal.history().front() > signal.history().back());
  TEST_CHECK(signal.history().back() == 0.0F);
  for (int i = 0; i < 128; ++i)
    signal.step(50.0F, true);
  for (float value : signal.history())
    TEST_CHECK(value == 0.0F);
}
