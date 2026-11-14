#pragma once

#include <chrono>
#include <string>
#include <utility>

namespace Fast::Oot3d {

// The host notice's text and time (graphics_settings_window.h: PostHostNotice,
// DrawHostNotice). Its time runs from the first frame that shows it, not from
// the post: the launch update check ends about a second after start, while
// the renderer preparation screen still covers the frame, and a 10 s notice
// timed from the post was over before the game was visible.
class HostNoticeTimer {
  public:
    using Clock = std::chrono::steady_clock;

    // Replaces the current notice. 3 s, a failure 8 s (they point the
    // player at the settings), or `duration`.
    void Post(std::string text, bool failure, std::chrono::seconds duration) {
        if (duration <= std::chrono::seconds::zero()) duration = std::chrono::seconds(failure ? 8 : 3);
        mText = std::move(text);
        mFailure = failure;
        mDuration = duration;
        mWaiting = true;
    }

    // A frame that can show the notice asks whether to draw it; `covered`
    // (something hides the frame) keeps a waiting notice waiting. The first
    // uncovered frame starts the time.
    [[nodiscard]] bool Show(Clock::time_point now, bool covered) noexcept {
        if (mWaiting) {
            if (covered) return false;
            mWaiting = false;
            mUntil = now + mDuration;
        }
        return !covered && now < mUntil;
    }

    [[nodiscard]] const std::string& Text() const noexcept { return mText; }
    [[nodiscard]] bool Failure() const noexcept { return mFailure; }

  private:
    std::string mText;
    bool mFailure = false;
    bool mWaiting = false;
    Clock::duration mDuration{};
    Clock::time_point mUntil{};
};

} // namespace Fast::Oot3d
