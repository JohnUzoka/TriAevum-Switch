// Default settings (oot3d_config_defaults.h): a first install gets every
// default, a later start writes none again, the player's files and removals
// are kept, unknown files are never touched, an install made from a zip that
// shipped config/ is recognised, a default a later release adds is written
// once, and copying the new zip's tree over an install leaves config/ as it
// was. Plain main(); exits non-zero on failure.
#include "oot3d_config_defaults.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace Oot3dNativeGame;
namespace fs = std::filesystem;

int gFailures = 0;

void Check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++gFailures;
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

std::string Read(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void Write(const fs::path &path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Every regular file under `root` with its bytes, by relative path.
std::map<std::string, std::string> Snapshot(const fs::path &root) {
  std::map<std::string, std::string> files;
  if (!fs::exists(root)) return files;
  for (const auto &entry : fs::recursive_directory_iterator(root))
    if (entry.is_regular_file()) files[fs::relative(entry.path(), root).generic_string()] = Read(entry.path());
  return files;
}

std::set<std::string> RecordNames(const fs::path &config) {
  std::set<std::string> names;
  std::ifstream file(config / kConfigDefaultsRecordName);
  std::string line;
  while (std::getline(file, line))
    if (!line.empty() && line.front() != '#') names.insert(line);
  return names;
}

std::set<std::string> AllNames(std::span<const ConfigDefault> defaults) {
  std::set<std::string> names;
  for (const auto &entry : defaults) names.emplace(entry.Name);
  return names;
}

struct TempDir {
  fs::path Path;
  explicit TempDir(const char *name) : Path(fs::temp_directory_path() / name) {
    fs::remove_all(Path);
    fs::create_directories(Path);
  }
  ~TempDir() {
    std::error_code ignored;
    fs::remove_all(Path, ignored);
  }
};

// The table is what releases 0.2.0 .. 0.2.6-test.2 shipped in config/ (19 files,
// SHA-checked against the zips when it was written; here: names and shapes).
void TestTable() {
  const auto defaults = ConfigDefaults();
  CHECK(defaults.size() == 19U);
  const std::set<std::string> expected = {
      "anisotropy", "deko3d_present_mailbox", "deko3d_present_thread", "deko3d_swapchain_images",
      "dynamic_resolution", "early_frame_slot", "fps60", "fps60_mode", "oot3d_controls.json", "pacer_vsync",
      "parallel_presentation", "renderer", "resolution_scale", "resolution_scale_docked", "sharpen",
      "skip_bottom_screen", "topscreen_ui.json", "unlock_master_quest", "upscaler"};
  CHECK(AllNames(defaults) == expected);
  CHECK(std::is_sorted(defaults.begin(), defaults.end(),
                       [](const ConfigDefault &a, const ConfigDefault &b) { return a.Name < b.Name; }));
  for (const auto &entry : defaults) {
    CHECK(entry.InLegacyZip);
    CHECK(entry.Name != kConfigDefaultsRecordName);
  }
  std::map<std::string_view, std::string_view> byName;
  for (const auto &entry : defaults) byName[entry.Name] = entry.Contents;
  CHECK(byName["renderer"] == "deko3d");
  CHECK(byName["fps60"].empty());
  CHECK(byName["fps60_mode"] == "legacy");
  CHECK(byName["resolution_scale"] == "3.00");
  CHECK(byName["resolution_scale_docked"] == "2.50");
  CHECK(byName["upscaler"] == "linear");
  CHECK(byName["oot3d_controls.json"].size() == 840U);
  CHECK(byName["topscreen_ui.json"].size() == 780U);
  CHECK(byName["topscreen_ui.json"].starts_with("{\n  \"schema\": \"oot3d_topscreen_ui_v2\""));
  CHECK(byName["oot3d_controls.json"].ends_with("}\n"));
  // No grass default: config/grass absent = Off (grass_switch_profile.h).
  CHECK(!byName.contains("grass"));
}

void TestFreshInstall() {
  TempDir root("oot3d_config_defaults_fresh");
  const fs::path config = root.Path / "config";
  const auto first = SeedConfigDefaults(config);
  CHECK(first.Errors.empty());
  CHECK(first.RecordCreated);
  CHECK(!first.LegacyInstall);
  CHECK(first.Seeded.size() == ConfigDefaults().size());
  for (const auto &entry : ConfigDefaults()) CHECK(Read(config / entry.Name) == entry.Contents);
  CHECK(RecordNames(config) == AllNames(ConfigDefaults()));
  // Exactly the old zip's config/ plus the record; no temporaries left.
  auto files = Snapshot(config);
  CHECK(files.size() == ConfigDefaults().size() + 1U);
  CHECK(files.contains(std::string(kConfigDefaultsRecordName)));
  CHECK(FormatConfigDefaultsLine(first).starts_with("config defaults: seeded=anisotropy,"));
  CHECK(FormatConfigDefaultsLine(first).ends_with(" record=new legacy=0"));

  // Second start: nothing written, nothing to log.
  const auto before = Snapshot(config);
  const auto second = SeedConfigDefaults(config);
  CHECK(second.Seeded.empty() && second.Errors.empty() && !second.RecordCreated);
  CHECK(Snapshot(config) == before);
  CHECK(FormatConfigDefaultsLine(second).empty());
}

// The player turns 60 FPS and Master Quest off (files removed), picks
// another renderer and a HUD scale: the next starts keep all of it.
void TestPlayerChoicesKept() {
  TempDir root("oot3d_config_defaults_choices");
  const fs::path config = root.Path / "config";
  (void)SeedConfigDefaults(config);
  fs::remove(config / "fps60");
  fs::remove(config / "unlock_master_quest");
  Write(config / "renderer", "opengl\n");
  Write(config / "topscreen_ui.json", "{\"hud_scale\": 1.0}\n");
  Write(config / "grass", "draw\n");
  const auto before = Snapshot(config);
  for (int start = 0; start < 3; ++start) {
    const auto result = SeedConfigDefaults(config);
    CHECK(result.Seeded.empty() && result.Errors.empty());
  }
  CHECK(Snapshot(config) == before);
  CHECK(!fs::exists(config / "fps60"));
  CHECK(Read(config / "renderer") == "opengl\n");
}

// Files the defaults do not name (grass, the TopScreen mod's pack, update
// channel, a folder) are never read or changed, and an existing file whose
// default the record lacks is the player's: kept and recorded.
void TestUnknownAndExistingFilesUntouched() {
  TempDir root("oot3d_config_defaults_unknown");
  const fs::path config = root.Path / "config";
  Write(config / kConfigDefaultsRecordName, "# comment\r\nanisotropy\r\n\r\n");
  Write(config / "grass", "placements\n");
  Write(config / "grass_lod", "0\n");
  Write(config / "atlas_overrides.o3tu", std::string(64, '\x7f'));
  Write(config / "update_channel", "test\n");
  Write(config / "topscreen_hd" / "a.png", "png");
  Write(config / "sharpen", "1");
  Write(config / "oot3d_controls.json", "{}\n");
  const auto result = SeedConfigDefaults(config);
  CHECK(result.Errors.empty());
  CHECK(!result.RecordCreated && !result.LegacyInstall);
  CHECK(std::find(result.Seeded.begin(), result.Seeded.end(), "sharpen") == result.Seeded.end());
  CHECK(std::find(result.Seeded.begin(), result.Seeded.end(), "anisotropy") == result.Seeded.end());
  CHECK(std::find(result.Seeded.begin(), result.Seeded.end(), "renderer") != result.Seeded.end());
  CHECK(result.Seeded.size() == ConfigDefaults().size() - 3U);
  CHECK(!fs::exists(config / "anisotropy")); // recorded before: a removal
  CHECK(Read(config / "sharpen") == "1");
  CHECK(Read(config / "oot3d_controls.json") == "{}\n");
  CHECK(Read(config / "grass") == "placements\n");
  CHECK(Read(config / "grass_lod") == "0\n");
  CHECK(Read(config / "atlas_overrides.o3tu") == std::string(64, '\x7f'));
  CHECK(Read(config / "update_channel") == "test\n");
  CHECK(Read(config / "topscreen_hd" / "a.png") == "png");
  CHECK(RecordNames(config) == AllNames(ConfigDefaults()));
}

// An install set up from a 0.2.x zip (config/ from the zip, then the player's
// changes), first started with this runtime: nothing is written back.
void TestLegacyInstall() {
  TempDir root("oot3d_config_defaults_legacy");
  const fs::path config = root.Path / "config";
  for (const auto &entry : ConfigDefaults())
    if (entry.Name != "fps60" && entry.Name != "dynamic_resolution") Write(config / entry.Name, entry.Contents);
  Write(config / "renderer", "opengl");
  const auto before = Snapshot(config);
  const auto result = SeedConfigDefaults(config);
  CHECK(result.Errors.empty());
  CHECK(result.RecordCreated && result.LegacyInstall);
  CHECK(result.Seeded.empty());
  CHECK(!fs::exists(config / "fps60"));
  CHECK(!fs::exists(config / "dynamic_resolution"));
  CHECK(RecordNames(config) == AllNames(ConfigDefaults()));
  auto after = Snapshot(config);
  after.erase(std::string(kConfigDefaultsRecordName));
  CHECK(after == before);
  CHECK(FormatConfigDefaultsLine(result) == "config defaults: seeded=none record=new legacy=1");

  // Even one surviving zip file marks a legacy install.
  TempDir sparse("oot3d_config_defaults_legacy_sparse");
  Write(sparse.Path / "config" / "topscreen_ui.json", "{}\n");
  const auto sparseResult = SeedConfigDefaults(sparse.Path / "config");
  CHECK(sparseResult.LegacyInstall && sparseResult.Seeded.empty());
  CHECK(!fs::exists(sparse.Path / "config" / "renderer"));

  // Unknown files alone (grass, logs of a dev setup) are not a zip install.
  TempDir other("oot3d_config_defaults_not_legacy");
  Write(other.Path / "config" / "grass", "draw\n");
  const auto otherResult = SeedConfigDefaults(other.Path / "config");
  CHECK(!otherResult.LegacyInstall);
  CHECK(otherResult.Seeded.size() == ConfigDefaults().size());
  CHECK(Read(other.Path / "config" / "grass") == "draw\n");
}

// A default a later release adds (not in the old zips): written once into an
// install that lacks it, legacy or not; removed by the player, it stays away.
void TestDefaultAddedLater() {
  const std::array<ConfigDefault, 3> defaults{{
      {"anisotropy", "1", true},
      {"new_setting", "on\n", false},
      {"renderer", "deko3d", true},
  }};
  TempDir root("oot3d_config_defaults_later");
  const fs::path config = root.Path / "config";
  Write(config / "renderer", "opengl");
  const auto first = SeedConfigDefaults(config, defaults);
  CHECK(first.LegacyInstall);
  CHECK(first.Seeded == std::vector<std::string>{"new_setting"});
  CHECK(Read(config / "new_setting") == "on\n");
  CHECK(!fs::exists(config / "anisotropy"));
  fs::remove(config / "new_setting");
  const auto second = SeedConfigDefaults(config, defaults);
  CHECK(second.Seeded.empty());
  CHECK(!fs::exists(config / "new_setting"));

  // An install with a record from before the default existed.
  TempDir recorded("oot3d_config_defaults_later_recorded");
  Write(recorded.Path / "config" / kConfigDefaultsRecordName, "anisotropy\nrenderer\n");
  const auto third = SeedConfigDefaults(recorded.Path / "config", defaults);
  CHECK(third.Seeded == std::vector<std::string>{"new_setting"});
  CHECK(!fs::exists(recorded.Path / "config" / "renderer"));
  CHECK(RecordNames(recorded.Path / "config") ==
        (std::set<std::string>{"anisotropy", "new_setting", "renderer"}));
}

// config/ that cannot be made: an error, nothing recorded, retried next time.
void TestWriteFailure() {
  TempDir root("oot3d_config_defaults_failure");
  Write(root.Path / "config", "not a folder");
  const auto result = SeedConfigDefaults(root.Path / "config");
  CHECK(!result.Errors.empty());
  CHECK(result.Seeded.empty());
  CHECK(Read(root.Path / "config") == "not a folder");
  CHECK(FormatConfigDefaultsLine(result).find(" errors=config/: ") != std::string::npos);
}

// Copy a release tree over an install the way a player does (every folder
// merged, files replaced). The new zip's tree (release-fork.sh: no config/)
// leaves the settings as they were; the old one (config/ shipped) did not.
void TestZipCopiedOverInstall() {
  TempDir root("oot3d_config_defaults_zip_copy");
  const fs::path install = root.Path / "sd" / "switch" / "oot3dre";
  const fs::path config = install / "config";
  (void)SeedConfigDefaults(config);
  fs::remove(config / "fps60");
  Write(config / "renderer", "opengl");
  Write(config / "grass", "draw\n");
  Write(install / "savedata" / "save00.bin", "save");
  const auto settings = Snapshot(config);

  const fs::path zipRoot = root.Path / "zip" / "switch" / "oot3dre";
  Write(zipRoot / "oot3d_native_game.nro", "nro");
  Write(zipRoot / "resources.o2r", "o2r");
  Write(zipRoot / "shadercache" / "deko3d.pack", "pack");
  Write(zipRoot / "shadercache" / "deko3d_reference.pack", "ref");
  Write(zipRoot / "licenses" / "LICENSE", "license");
  fs::copy(root.Path / "zip", root.Path / "sd",
           fs::copy_options::recursive | fs::copy_options::overwrite_existing);
  CHECK(Snapshot(config) == settings);
  const auto start = SeedConfigDefaults(config);
  CHECK(start.Seeded.empty() && start.Errors.empty());
  CHECK(Snapshot(config) == settings);
  CHECK(Read(install / "oot3d_native_game.nro") == "nro");
  CHECK(Read(install / "savedata" / "save00.bin") == "save");

  // What the 0.2.0 .. 0.2.6-test.2 zips did: config/fps60 back, the renderer reset.
  for (const auto &entry : ConfigDefaults()) Write(root.Path / "old" / "switch" / "oot3dre" / "config" / entry.Name, entry.Contents);
  fs::copy(root.Path / "old", root.Path / "sd", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
  CHECK(Snapshot(config) != settings);
  CHECK(Read(config / "renderer") == "deko3d");
}

} // namespace

int main() {
  TestTable();
  TestFreshInstall();
  TestPlayerChoicesKept();
  TestUnknownAndExistingFilesUntouched();
  TestLegacyInstall();
  TestDefaultAddedLater();
  TestWriteFailure();
  TestZipCopiedOverInstall();
  if (gFailures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", gFailures);
    return EXIT_FAILURE;
  }
  std::puts("oot3d_config_defaults_tests: ok");
  return EXIT_SUCCESS;
}
