#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pkgfile {

// Renders a live, in-place, two-line-per-repo progress display (download,
// then repack) to stdout while `pkgfile -u` runs. Downloads and repacks
// happen concurrently across repos -- and repacking itself runs on a
// separate worker thread per repo -- so every update is synchronized and
// safe to call from any thread.
//
// Falls back to doing nothing at all when stdout isn't a terminal (a pipe,
// a log file, a test harness capturing output): live-redrawing with ANSI
// cursor movement would just corrupt non-interactive output. Callers should
// check IsInteractive() and print their own plain-text status lines instead
// in that case.
class ProgressDisplay {
 public:
  enum class Stage {
    kPending,
    kActive,
    kDone,
    kSkipped,
    kFailed,
  };

  // `repo_names` are shown in this order and fix the number of rows; there's
  // no way to add or remove a repo after construction.
  explicit ProgressDisplay(std::vector<std::string> repo_names);
  // Restores cursor visibility if Finish() was never reached (an early
  // return on error, say) -- never leaves the user's terminal cursor
  // hidden.
  ~ProgressDisplay();

  bool IsInteractive() const { return is_tty_; }

  // `total <= 0` means "size unknown", and just the raw transferred bytes
  // are shown rather than a percentage bar.
  void UpdateDownload(size_t index, int64_t now, int64_t total);
  // `elapsed_seconds` is only shown (and only meaningful) for kDone.
  void FinishDownload(size_t index, Stage stage, double elapsed_seconds = 0.0);

  void UpdateRepack(size_t index, int64_t now, int64_t total);
  void FinishRepack(size_t index, Stage stage, double elapsed_seconds = 0.0);

  // Redraws one final time and moves the cursor past the display, so
  // whatever the caller prints next (a summary line, say) starts on a fresh
  // line below it rather than overwriting it. Safe to call more than once.
  void Finish();

 private:
  struct Row {
    std::string name;
    Stage dl_stage = Stage::kPending;
    int64_t dl_now = 0;
    int64_t dl_total = 0;
    double dl_elapsed = 0.0;
    Stage rp_stage = Stage::kPending;
    int64_t rp_now = 0;
    int64_t rp_total = 0;
    double rp_elapsed = 0.0;
  };

  // Both must be called with mu_ held.
  void MaybeRedraw(bool force);
  // `show_bytes` is false for the repack row: its total is really just a
  // proxy (the archive's compressed size, compared against bytes consumed
  // from it) rather than a meaningful "amount of work" figure, so only the
  // percentage and elapsed time are worth showing there.
  std::string RenderLine(std::string_view label, Stage stage, int64_t now,
                         int64_t total, double elapsed_seconds,
                         bool show_bytes) const;

  std::mutex mu_;
  std::vector<Row> rows_;
  size_t name_width_ = 0;
  bool is_tty_;
  bool first_draw_ = true;
  bool finished_ = false;
  std::chrono::steady_clock::time_point last_redraw_;
};

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
