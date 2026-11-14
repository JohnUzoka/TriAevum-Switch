#include "fast/oot3d/host_notice.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

} // namespace

int main() {
  using namespace Fast::Oot3d;
  using namespace std::chrono_literals;
  const HostNoticeTimer::Clock::time_point start{};
  {
    // Nothing posted: nothing drawn.
    HostNoticeTimer notice;
    Require(!notice.Show(start, false), "no notice");
  }
  {
    // The launch update check: posted at start under the preparation screen,
    // which covers the frame for 40 s; the 10 s run from the first shown frame.
    HostNoticeTimer notice;
    notice.Post("Version 0.2.6 is available: L3+R3 > Updates", false, 10s);
    for (auto now = start; now < start + 40s; now += 1s)
      Require(!notice.Show(now, true), "waits while covered");
    Require(notice.Show(start + 40s, false), "first uncovered frame shows it");
    Require(notice.Show(start + 49s, false), "still shown 9 s later");
    Require(!notice.Show(start + 50s, false), "over 10 s after it showed");
    Require(notice.Text() == "Version 0.2.6 is available: L3+R3 > Updates", "text kept");
    Require(!notice.Failure(), "not a failure");
  }
  {
    // A notice posted in the game shows at once; default times 3 s and 8 s.
    HostNoticeTimer notice;
    notice.Post("Saved to slot 1", false, 0s);
    Require(notice.Show(start, false), "shown at once");
    Require(notice.Show(start + 2s, false), "3 s: 2 s in");
    Require(!notice.Show(start + 3s, false), "3 s: over");
    notice.Post("Load failed", true, 0s);
    Require(notice.Show(start + 10s, false), "a later notice replaces an expired one");
    Require(notice.Failure(), "failure kept");
    Require(notice.Show(start + 17s, false), "8 s: 7 s in");
    Require(!notice.Show(start + 18s, false), "8 s: over");
  }
  {
    // A later notice replaces a shown one and gets its own full time.
    HostNoticeTimer notice;
    notice.Post("first", false, 0s);
    Require(notice.Show(start, false), "first shown");
    notice.Post("second", false, 0s);
    Require(notice.Show(start + 2s, false), "second shown");
    Require(notice.Text() == "second", "second's text");
    Require(notice.Show(start + 4s, false), "second's own 3 s");
    Require(!notice.Show(start + 5s, false), "second over");
  }
  {
    // Covered after it started: hidden, and its time keeps running.
    HostNoticeTimer notice;
    notice.Post("shown", false, 0s);
    Require(notice.Show(start, false), "shown");
    Require(!notice.Show(start + 1s, true), "hidden while covered");
    Require(notice.Show(start + 2s, false), "back");
    Require(!notice.Show(start + 3s, false), "over");
  }
  std::puts("host notice tests passed");
  return 0;
}
