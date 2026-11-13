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
  constexpr auto kN64 = IdleAnimations::N64;
  GameplayOptionsLog log;
  const auto start = log.Take();
  Require(Contains(start, "gameplay: quick_put_away=off damage=normal master_quest_unmirrored=off ") &&
              Contains(start, " idle_animations=3ds simple_shadow=on shadow_cutscenes=off ") &&
              Contains(start, "config/idle_animations"),
          "the defaults at start");
  Require(!Contains(start, "applies at the next") && !Contains(start, "shadow_method"),
          "nothing pending at start, and no shadow method (one decal now)");
  Require(!log.Take().has_value(), "an unchanged setting is not logged again");
  Require(Contains(log.Take(true, 2U, false, false, kN64, false, false), "quick_put_away=on damage=2x "),
          "a change");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, false, false, kN64, false, false), "damage=one hit "),
          "one hit");
  const auto pending = log.Take(true, kDamageMultiplierOneHit, true, false, kN64, false, false);
  Require(Contains(pending, "master_quest_unmirrored=on (applies at the next file load or pause menu close) "
                            "idle_animations=n64 "),
          "unmirrored before the game's read");
  const auto applied = log.Take(true, kDamageMultiplierOneHit, true, true, kN64, false, false);
  Require(Contains(applied, "master_quest_unmirrored=on idle_animations=n64 simple_shadow=off ") &&
              !Contains(applied, "applies at the next"),
          "logged again once applied");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, IdleAnimations::Mixed, false, false),
                   " idle_animations=mixed "),
          "the idle animations mixed");
  constexpr auto k3ds = IdleAnimations::Original3ds;
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, k3ds, false, false), " idle_animations=3ds "),
          "the 3DS idle animations");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, k3ds, true, false), "simple_shadow=on "),
          "the simple shadow");
  Require(Contains(log.Take(true, kDamageMultiplierOneHit, true, true, k3ds, true, true), "shadow_cutscenes=on "),
          "the shadow in cutscenes");
  Require(!log.Take(true, kDamageMultiplierOneHit, true, true, k3ds, true, true).has_value(), "then quiet");
}

} // namespace

int main() {
  TestLine();
  std::puts("oot3d_gameplay_options_log_tests: ok");
  return 0;
}
