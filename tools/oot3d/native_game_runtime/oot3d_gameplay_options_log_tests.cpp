// Game log line of the Single Screen gameplay options (oot3d_gameplay_options_log.h).
#include "oot3d_gameplay_options_log.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

bool Contains(const std::optional<std::string> &line, std::string_view text) {
  return line.has_value() && line->find(text) != std::string::npos;
}

void TestLine() {
  GameplayOptionsLog log;
  const auto start = log.Take();
  Require(Contains(start, "gameplay: quick_put_away=off damage=normal master_quest_unmirrored=off ") &&
              Contains(start, " n64_idle_animations=on simple_shadow=on shadow_cutscenes=off ") &&
              Contains(start, "config/no_n64_idle_animations"),
          "the defaults at start");
  Require(!Contains(start, "applies at the next") && !Contains(start, "shadow_method"),
          "nothing pending at start, and no shadow method (one decal now)");
  Require(!log.Take().has_value(), "an unchanged setting is not logged again");
  Require(Contains(log.Take(true, 2U, false, false, true, false, false), "quick_put_away=on damage=2x "),
          "a change");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, false, false, true, false, false), "damage=one hit "),
          "one hit");
  const auto pending = log.Take(true, kDamageMultiplierOneHit, true, false, true, false, false);
  Require(Contains(pending, "master_quest_unmirrored=on (applies at the next file load or pause menu close) "
                            "n64_idle_animations=on "),
          "unmirrored before the game's read");
  const auto applied = log.Take(true, kDamageMultiplierOneHit, true, true, true, false, false);
  Require(Contains(applied, "master_quest_unmirrored=on n64_idle_animations=on simple_shadow=off ") &&
              !Contains(applied, "applies at the next"),
          "logged again once applied");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, false, false, false),
                   " n64_idle_animations=off "),
          "the N64 idle animations off");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, false, true, false), "simple_shadow=on "),
          "the simple shadow");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, false, true, true), "shadow_cutscenes=on "),
          "the shadow in cutscenes");
  Require(!log.Take(true, kDamageMultiplierOneHit, true, true, false, true, true).has_value(), "then quiet");
}

} // namespace

int main() {
  TestLine();
  std::puts("oot3d_gameplay_options_log_tests: ok");
  return 0;
}
