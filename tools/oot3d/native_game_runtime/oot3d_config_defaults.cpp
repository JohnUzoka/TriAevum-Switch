#include "oot3d_config_defaults.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <system_error>

namespace Oot3dNativeGame {

namespace {

namespace fs = std::filesystem;

// The author's defaults, as release zips 0.2.0 .. 0.2.6-test.2 shipped them in
// config/ (from the reference install runs/p7b): deko3d renderer, 60 FPS
// (legacy pacing), dynamic resolution at 3.00x handheld / 2.50x docked with
// the linear upscaler, 4x anisotropic filter, the deko3d present thread and
// mailbox, controller profile and the TopScreen layout. Sorted by name.
constexpr std::array kDefaults = std::to_array<ConfigDefault>({
    {"anisotropy", "1", true},
    {"deko3d_present_mailbox", "", true},
    {"deko3d_present_thread", "", true},
    {"deko3d_swapchain_images", "4", true},
    {"dynamic_resolution", "", true},
    {"early_frame_slot", "1", true},
    {"fps60", "", true},
    {"fps60_mode", "legacy", true},
    {"oot3d_controls.json", R"json({
  "schema": "oot3d_native_controls_v1",
  "profile": "controller",
  "devices": {
    "keyboard": false,
    "mouse": false,
    "controller": true
  },
  "capture_mouse": false,
  "controller_guid": "",
  "analog": {
    "movement_stick": "left",
    "movement_dead_zone_percent": 18,
    "look_dead_zone_percent": 20,
    "trigger_dead_zone_percent": 20
  },
  "aim": {
    "source": "automatic",
    "mouse_degrees_per_pixel": 0.35,
    "right_stick_max_degrees_per_second": 180.0,
    "gyroscope_sensitivity": 1.0,
    "accelerometer_sensitivity": 1.0,
    "invert_x": false,
    "invert_y": false
  },
  "free_camera": {
    "source": "right_stick",
    "mouse_units_per_pixel": 4.0,
    "motion_sensitivity": 1.0
  },
  "calibration": {
    "gyroscope_bias_dps": [0.0, 0.0, 0.0],
    "accelerometer_neutral": [0.0, -1.0, 0.0]
  }
}
)json", true},
    {"pacer_vsync", "", true},
    {"parallel_presentation", "", true},
    {"renderer", "deko3d", true},
    {"resolution_scale", "3.00", true},
    {"resolution_scale_docked", "2.50", true},
    {"sharpen", "0", true},
    {"skip_bottom_screen", "", true},
    {"topscreen_ui.json", R"json({
  "schema": "oot3d_topscreen_ui_v2",
  "hud_layout": "normal",
  "hud_scale": 0.75,
  "hud_margin_x": 4,
  "hud_margin_y": 1,
  "magic_bar_y": 0,
  "minimap_visible": true,
  "render_hud": true,
  "render_dpad_icons": true,
  "render_items_hint": true,
  "select_action": "save_screen",
  "exit_items_to_save_screen": true,
  "camera_zoom_percent": 100,
  "camera_fov_percent": 100,
  "dpad_child": ["view", "ocarina", "boomerang", "slingshot"],
  "dpad_adult": ["view", "ocarina", "iron_boots", "hover_boots"],
  "free_camera_enabled": true,
  "free_camera_speed_level": 3,
  "free_camera_smoothing": "default",
  "free_camera_invert_x": false,
  "free_camera_invert_y": false,
  "c_stick_aim_speed_level": 3,
  "c_stick_aim_invert_x": false,
  "c_stick_aim_invert_y": false
}
)json", true},
    {"unlock_master_quest", "", true},
    {"upscaler", "linear", true},
});

bool Exists(const fs::path &path) {
  std::error_code error;
  // A broken link or an entry that cannot be inspected still holds the name:
  // never written over (only "not found" is missing).
  return fs::symlink_status(path, error).type() != fs::file_type::not_found;
}

// Through a temporary file and a rename: a power cut leaves the old file or
// the new one, never half of it.
bool WriteWhole(const fs::path &path, std::string_view contents) {
  fs::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file.flush()) {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      return false;
    }
  }
  std::error_code error;
  fs::rename(temporary, path, error);
  if (error) fs::remove(temporary, error);
  return !error;
}

std::string_view Trim(std::string_view text) {
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!text.empty() && space(text.front())) text.remove_prefix(1);
  while (!text.empty() && space(text.back())) text.remove_suffix(1);
  return text;
}

// One name per line; '#' starts a comment line. False when there is no record.
bool ReadRecord(const fs::path &path, std::set<std::string, std::less<>> &names) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    const std::string_view name = Trim(line);
    if (!name.empty() && name.front() != '#') names.emplace(name);
  }
  return true;
}

std::string FormatRecord(const std::set<std::string, std::less<>> &names) {
  std::string text = "# Default settings this install has received (oot3d_config_defaults.h).\n"
                     "# The game never writes a default listed here again. Delete the whole\n"
                     "# config folder to start over from the defaults.\n";
  for (const auto &name : names) text.append(name).push_back('\n');
  return text;
}

} // namespace

std::span<const ConfigDefault> ConfigDefaults() noexcept { return kDefaults; }

ConfigDefaultsResult SeedConfigDefaults(const fs::path &configDirectory,
                                        std::span<const ConfigDefault> defaults) {
  ConfigDefaultsResult result;
  const fs::path recordPath = configDirectory / kConfigDefaultsRecordName;
  std::set<std::string, std::less<>> applied;
  if (!ReadRecord(recordPath, applied)) {
    result.RecordCreated = true;
    result.LegacyInstall = std::any_of(defaults.begin(), defaults.end(), [&](const ConfigDefault &entry) {
      return entry.InLegacyZip && Exists(configDirectory / entry.Name);
    });
    if (result.LegacyInstall)
      for (const auto &entry : defaults)
        if (entry.InLegacyZip) applied.emplace(entry.Name);
  }
  const std::size_t before = applied.size();
  std::error_code error;
  fs::create_directories(configDirectory, error);
  if (error) {
    result.Errors.push_back("config/: " + error.message());
    return result;
  }
  for (const auto &entry : defaults) {
    if (applied.contains(entry.Name)) continue;
    const fs::path path = configDirectory / entry.Name;
    if (!Exists(path)) {
      if (!WriteWhole(path, entry.Contents)) {
        result.Errors.push_back(std::string(entry.Name) + ": not written");
        continue;
      }
      result.Seeded.emplace_back(entry.Name);
    }
    applied.emplace(entry.Name);
  }
  if ((result.RecordCreated || applied.size() != before) && !WriteWhole(recordPath, FormatRecord(applied)))
    result.Errors.push_back(std::string(kConfigDefaultsRecordName) + ": not written");
  return result;
}

std::string FormatConfigDefaultsLine(const ConfigDefaultsResult &result) {
  if (result.Seeded.empty() && result.Errors.empty() && !result.RecordCreated) return {};
  std::string line = "config defaults: seeded=";
  for (std::size_t index = 0; index < result.Seeded.size(); ++index) {
    if (index != 0) line.push_back(',');
    line += result.Seeded[index];
  }
  if (result.Seeded.empty()) line += "none";
  line += result.RecordCreated ? " record=new" : " record=kept";
  line += result.LegacyInstall ? " legacy=1" : " legacy=0";
  if (!result.Errors.empty()) {
    line += " errors=";
    for (std::size_t index = 0; index < result.Errors.size(); ++index) {
      if (index != 0) line += "; ";
      line += result.Errors[index];
    }
  }
  return line;
}

} // namespace Oot3dNativeGame
