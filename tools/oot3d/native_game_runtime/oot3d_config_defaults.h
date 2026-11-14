#pragma once

// Default settings on the Switch (SWITCH_PORT.md, "Settings kept across
// updates"): the files a first install starts with in
// sdmc:/switch/oot3dre/config/ (deko3d renderer, 60 FPS, dynamic resolution,
// controls, TopScreen layout...). The release zips up to 0.2.6-test.2 carried them in
// config/, so copying a new zip over an install put every one of them back
// over the player's choices. The zip has no config/ now: the defaults are
// compiled in and written by the game at start-up (oot3d_native_game.cpp,
// before anything reads config/), each at most once per install.
//
// - config/defaults_applied lists the defaults this install has received. A
//   default in it is never written again, so a setting whose choice is the
//   file's absence (config/fps60 removed = 30 FPS, unlock_master_quest...)
//   stays as the player left it; a default not in it is written when its file
//   is missing (a new install, or a default a later release adds) and an
//   existing file is the player's: kept, and recorded.
// - An install without the record that has one of the files every zip up to
//   0.2.6-test.2 shipped (InLegacyZip) was set up from such a zip: those defaults
//   count as received (written or deliberately removed), only the others can
//   be written.
// - Nothing else in config/ is read or changed. Deleting config/ starts over
//   from the defaults.
//
// Plain code: tested on the host (oot3d_config_defaults_tests).

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

struct ConfigDefault {
  std::string_view Name;     // file name in config/
  std::string_view Contents; // its exact bytes
  // Shipped in config/ by every release zip 0.2.0 .. 0.2.6-test.2.
  bool InLegacyZip = false;
};

// The defaults a first install gets: the files release zips 0.2.0 .. 0.2.6-test.2
// shipped in config/, byte for byte.
std::span<const ConfigDefault> ConfigDefaults() noexcept;

inline constexpr std::string_view kConfigDefaultsRecordName = "defaults_applied";

struct ConfigDefaultsResult {
  std::vector<std::string> Seeded; // defaults written this start
  std::vector<std::string> Errors; // one line per file that failed
  bool RecordCreated = false;      // config/defaults_applied did not exist
  bool LegacyInstall = false;      // no record, but a legacy zip's file
};

// Writes each default of `defaults` that the record does not list and whose
// file is missing, then records every default it wrote or found. A failed
// write is not recorded, so the next start tries again.
ConfigDefaultsResult SeedConfigDefaults(const std::filesystem::path &configDirectory,
                                        std::span<const ConfigDefault> defaults = ConfigDefaults());

// "config defaults: seeded=fps60,renderer record=new legacy=0 errors=..." for
// logs/oot3d_native_game.log; empty when nothing changed.
std::string FormatConfigDefaultsLine(const ConfigDefaultsResult &result);

} // namespace Oot3dNativeGame
