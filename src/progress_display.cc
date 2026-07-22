#include "progress_display.hh"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>

namespace pkgfile {

namespace {

constexpr int kBarWidth = 24;
constexpr auto kMinRedrawInterval = std::chrono::milliseconds(80);

std::string HumanizeBytes(int64_t bytes) {
  static constexpr std::array labels{"B", "KiB", "MiB", "GiB", "TiB"};

  double val = static_cast<double>(bytes);
  size_t i = 0;
  while (val >= 1024.0 && i + 1 < labels.size()) {
    val /= 1024.0;
    ++i;
  }

  if (i == 0) {
    return std::format("{:.0f} {}", val, labels[i]);
  }
  return std::format("{:.1f} {}", val, labels[i]);
}

std::string RenderBar(int64_t now, int64_t total) {
  const int64_t clamped =
      std::clamp<int64_t>(now, 0, std::max<int64_t>(total, 0));
  const int filled =
      total > 0 ? static_cast<int>(kBarWidth * clamped / total) : 0;

  std::string bar(kBarWidth, ' ');
  std::fill_n(bar.begin(), filled, '=');
  if (filled > 0 && filled < kBarWidth) {
    bar[filled] = '>';
  }
  return bar;
}

}  // namespace

ProgressDisplay::ProgressDisplay(std::vector<std::string> repo_names)
    : is_tty_(isatty(fileno(stdout)) != 0) {
  rows_.reserve(repo_names.size());
  for (auto& name : repo_names) {
    name_width_ = std::max(name_width_, name.size());
    rows_.push_back(Row{.name = std::move(name)});
  }
}

ProgressDisplay::~ProgressDisplay() { Finish(); }

std::string ProgressDisplay::RenderLine(std::string_view label, Stage stage,
                                        int64_t now, int64_t total,
                                        double elapsed_seconds,
                                        bool show_bytes) const {
  switch (stage) {
    case Stage::kPending:
      return std::format("  {:<8} pending", label);
    case Stage::kSkipped:
      return std::format("  {:<8} skipped", label);
    case Stage::kFailed:
      return std::format("  {:<8} failed", label);
    case Stage::kDone:
      if (show_bytes) {
        return std::format("  {:<8} [{}] done ({} in {:.2f}s)", label,
                           std::string(kBarWidth, '='), HumanizeBytes(now),
                           elapsed_seconds);
      }
      return std::format("  {:<8} [{}] done ({:.2f}s)", label,
                         std::string(kBarWidth, '='), elapsed_seconds);
    case Stage::kActive:
      if (total <= 0) {
        // Unknown size (no Content-Length, or we haven't sized the repack
        // input yet): show raw progress instead of a bar we can't fill
        // meaningfully. There's nothing byte-shaped left to show at all if
        // the caller doesn't want bytes either.
        return show_bytes
                   ? std::format("  {:<8} {} so far", label, HumanizeBytes(now))
                   : std::format("  {:<8} running", label);
      }
      if (show_bytes) {
        return std::format("  {:<8} [{}] {:>3}%  {}/{}", label,
                           RenderBar(now, total),
                           std::clamp<int64_t>(now * 100 / total, 0, 100),
                           HumanizeBytes(now), HumanizeBytes(total));
      }
      return std::format("  {:<8} [{}] {:>3}%", label, RenderBar(now, total),
                         std::clamp<int64_t>(now * 100 / total, 0, 100));
  }
  return std::string();
}

void ProgressDisplay::MaybeRedraw(bool force) {
  if (!is_tty_ || finished_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!force && !first_draw_ && now - last_redraw_ < kMinRedrawInterval) {
    return;
  }
  last_redraw_ = now;

  std::string out;
  if (first_draw_) {
    first_draw_ = false;
    out += "\x1b[?25l";  // hide cursor while we're redrawing in place
  } else {
    out += std::format("\x1b[{}A", rows_.size() * 2);  // cursor up to the top
  }

  for (const auto& row : rows_) {
    out += std::format("  {:<{}}", row.name, name_width_);
    out += RenderLine("download", row.dl_stage, row.dl_now, row.dl_total,
                      row.dl_elapsed, /*show_bytes=*/true);
    out += "\x1b[K\n";
    out += std::string(name_width_ + 2, ' ');
    out += RenderLine("repack", row.rp_stage, row.rp_now, row.rp_total,
                      row.rp_elapsed, /*show_bytes=*/false);
    out += "\x1b[K\n";
  }

  fwrite(out.data(), 1, out.size(), stdout);
  fflush(stdout);
}

void ProgressDisplay::UpdateDownload(size_t index, int64_t now, int64_t total) {
  std::lock_guard<std::mutex> lock(mu_);
  Row& row = rows_[index];
  row.dl_stage = Stage::kActive;
  row.dl_now = now;
  row.dl_total = total;
  MaybeRedraw(/*force=*/false);
}

void ProgressDisplay::FinishDownload(size_t index, Stage stage,
                                     double elapsed_seconds) {
  std::lock_guard<std::mutex> lock(mu_);
  Row& row = rows_[index];
  row.dl_stage = stage;
  if (stage == Stage::kDone) {
    row.dl_now = row.dl_total;
    row.dl_elapsed = elapsed_seconds;
  }
  MaybeRedraw(/*force=*/true);
}

void ProgressDisplay::UpdateRepack(size_t index, int64_t now, int64_t total) {
  std::lock_guard<std::mutex> lock(mu_);
  Row& row = rows_[index];
  row.rp_stage = Stage::kActive;
  row.rp_now = now;
  row.rp_total = total;
  MaybeRedraw(/*force=*/false);
}

void ProgressDisplay::FinishRepack(size_t index, Stage stage,
                                   double elapsed_seconds) {
  std::lock_guard<std::mutex> lock(mu_);
  Row& row = rows_[index];
  row.rp_stage = stage;
  if (stage == Stage::kDone) {
    row.rp_now = row.rp_total;
    row.rp_elapsed = elapsed_seconds;
  }
  MaybeRedraw(/*force=*/true);
}

void ProgressDisplay::Finish() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!is_tty_ || finished_) {
    return;
  }
  MaybeRedraw(/*force=*/true);
  finished_ = true;
  fputs("\x1b[?25h", stdout);  // restore cursor visibility
  fflush(stdout);
}

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
