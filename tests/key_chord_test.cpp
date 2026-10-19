#include "core/input/key_chord.h"
#include "core/input/key_modifiers.h"
#include "test_check.h"

#include <array>
#include <string>
#include <xkbcommon/xkbcommon-keysyms.h>

int main() {
  for (char letter = 'A'; letter <= 'Z'; ++letter) {
    const auto chord = parseKeyChordSpec(std::string("Shift+") + letter);
    TEST_CHECK(chord.has_value());
    TEST_CHECK(keyChordMatches(*chord, static_cast<unsigned>(letter), KeyMod::Shift));
    TEST_CHECK(!keyChordMatches(*chord, static_cast<unsigned>(letter), 0));
    TEST_CHECK(!keyChordMatches(*chord, static_cast<unsigned>(letter), KeyMod::Shift | KeyMod::Ctrl));
    TEST_CHECK(!keyChordMatches(*chord, static_cast<unsigned>(letter), KeyMod::Shift | KeyMod::Alt));
  }

  struct CyrillicKey {
    const char* chord;
    unsigned sym;
  };
  constexpr std::array cyrillic{
      CyrillicKey{"Shift+Cyrillic_A", XKB_KEY_Cyrillic_A},
      CyrillicKey{"Shift+Cyrillic_BE", XKB_KEY_Cyrillic_BE},
      CyrillicKey{"Shift+Cyrillic_VE", XKB_KEY_Cyrillic_VE},
      CyrillicKey{"Shift+Cyrillic_GHE", XKB_KEY_Cyrillic_GHE},
      CyrillicKey{"Shift+Cyrillic_DE", XKB_KEY_Cyrillic_DE},
      CyrillicKey{"Shift+Cyrillic_IE", XKB_KEY_Cyrillic_IE},
      CyrillicKey{"Shift+Cyrillic_IO", XKB_KEY_Cyrillic_IO},
      CyrillicKey{"Shift+Cyrillic_ZHE", XKB_KEY_Cyrillic_ZHE},
      CyrillicKey{"Shift+Cyrillic_ZE", XKB_KEY_Cyrillic_ZE},
      CyrillicKey{"Shift+Cyrillic_I", XKB_KEY_Cyrillic_I},
      CyrillicKey{"Shift+Cyrillic_SHORTI", XKB_KEY_Cyrillic_SHORTI},
      CyrillicKey{"Shift+Cyrillic_KA", XKB_KEY_Cyrillic_KA},
      CyrillicKey{"Shift+Cyrillic_EL", XKB_KEY_Cyrillic_EL},
      CyrillicKey{"Shift+Cyrillic_EM", XKB_KEY_Cyrillic_EM},
      CyrillicKey{"Shift+Cyrillic_EN", XKB_KEY_Cyrillic_EN},
      CyrillicKey{"Shift+Cyrillic_O", XKB_KEY_Cyrillic_O},
      CyrillicKey{"Shift+Cyrillic_PE", XKB_KEY_Cyrillic_PE},
      CyrillicKey{"Shift+Cyrillic_ER", XKB_KEY_Cyrillic_ER},
      CyrillicKey{"Shift+Cyrillic_ES", XKB_KEY_Cyrillic_ES},
      CyrillicKey{"Shift+Cyrillic_TE", XKB_KEY_Cyrillic_TE},
      CyrillicKey{"Shift+Cyrillic_U", XKB_KEY_Cyrillic_U},
      CyrillicKey{"Shift+Cyrillic_EF", XKB_KEY_Cyrillic_EF},
      CyrillicKey{"Shift+Cyrillic_HA", XKB_KEY_Cyrillic_HA},
      CyrillicKey{"Shift+Cyrillic_TSE", XKB_KEY_Cyrillic_TSE},
      CyrillicKey{"Shift+Cyrillic_CHE", XKB_KEY_Cyrillic_CHE},
      CyrillicKey{"Shift+Cyrillic_SHA", XKB_KEY_Cyrillic_SHA},
      CyrillicKey{"Shift+Cyrillic_SHCHA", XKB_KEY_Cyrillic_SHCHA},
      CyrillicKey{"Shift+Cyrillic_HARDSIGN", XKB_KEY_Cyrillic_HARDSIGN},
      CyrillicKey{"Shift+Cyrillic_YERU", XKB_KEY_Cyrillic_YERU},
      CyrillicKey{"Shift+Cyrillic_SOFTSIGN", XKB_KEY_Cyrillic_SOFTSIGN},
      CyrillicKey{"Shift+Cyrillic_E", XKB_KEY_Cyrillic_E},
      CyrillicKey{"Shift+Cyrillic_YU", XKB_KEY_Cyrillic_YU},
      CyrillicKey{"Shift+Cyrillic_YA", XKB_KEY_Cyrillic_YA},
  };
  for (const auto& key : cyrillic) {
    const auto chord = parseKeyChordSpec(key.chord);
    TEST_CHECK(chord.has_value());
    TEST_CHECK(keyChordMatches(*chord, key.sym, KeyMod::Shift));
    TEST_CHECK(!keyChordMatches(*chord, key.sym, 0));
    TEST_CHECK(!keyChordMatches(*chord, key.sym, KeyMod::Shift | KeyMod::Ctrl));
  }

  const auto plain = parseKeyChordSpec("A");
  TEST_CHECK(plain.has_value());
  TEST_CHECK(keyChordMatches(*plain, XKB_KEY_a, 0));
  TEST_CHECK(!keyChordMatches(*plain, XKB_KEY_A, 0));
  TEST_CHECK(!keyChordMatches(*plain, XKB_KEY_A, KeyMod::Shift));
  const auto punctuation = parseKeyChordSpec("Shift+question");
  TEST_CHECK(punctuation.has_value());
  TEST_CHECK(keyChordMatches(*punctuation, XKB_KEY_question, KeyMod::Shift));
  TEST_CHECK(!keyChordMatches(*punctuation, XKB_KEY_slash, KeyMod::Shift));
  TEST_CHECK(!keyChordMatches(*punctuation, XKB_KEY_question, 0));
  const auto control = parseKeyChordSpec("Ctrl+Shift+J");
  TEST_CHECK(control.has_value());
  TEST_CHECK(keyChordMatches(*control, XKB_KEY_J, KeyMod::Ctrl | KeyMod::Shift));
  TEST_CHECK(!keyChordMatches(*control, XKB_KEY_J, KeyMod::Shift));
  return 0;
}
