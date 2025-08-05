#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace LUFS {

WindowManager::VisualizerWindow* window;

// Layout configuration constants
constexpr size_t LABEL_WIDTH = 25;
constexpr size_t LABEL_GAP = 10;
constexpr size_t PEAK_BAR_WIDTH = 5;
constexpr size_t PEAK_BARS_GAP = 2;
constexpr size_t LUFS_BAR_WIDTH = 10;
constexpr size_t LUFS_BAR_GAP = 5;
constexpr size_t LABEL_LINE_LENGTH = 5;
constexpr size_t LUFS_TEXT_BOX_PADDING = 4;
constexpr size_t LUFS_TEXT_BOX_VERTICAL_PADDING = 6;
constexpr size_t TOP_HEIGHT_PERCENT = 10;
constexpr size_t FONT_SIZE_LABELS = 10;
constexpr size_t FONT_SIZE_LUFS = 14;

void render() {
  if (!window)
    return;

  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  const float& lufs = DSP::LUFS::lufs;

  const float* color = Theme::colors.color;
  if (Theme::colors.loudness_main[3] > 1e-6f)
    color = Theme::colors.loudness_main;

  // Calculate layout positions
  const size_t topHeight = SDLWindow::height * (TOP_HEIGHT_PERCENT / 100.0f);
  const size_t barHeight = SDLWindow::height - topHeight;

  // Peak bars positions
  const size_t leftPeakX = LABEL_WIDTH + LABEL_GAP;
  const size_t rightPeakX = leftPeakX + PEAK_BAR_WIDTH + PEAK_BARS_GAP;

  // LUFS bar position
  const size_t lufsBarX = LABEL_WIDTH + LABEL_GAP + PEAK_BAR_WIDTH + PEAK_BARS_GAP + PEAK_BAR_WIDTH + LUFS_BAR_GAP;

  // Draw background bar
  Graphics::drawFilledRect(lufsBarX, 0, LUFS_BAR_WIDTH, SDLWindow::height, Theme::colors.bgaccent);

  // Draw peak background bars
  Graphics::drawFilledRect(leftPeakX, 0, PEAK_BAR_WIDTH, SDLWindow::height, Theme::colors.bgaccent);
  Graphics::drawFilledRect(rightPeakX, 0, PEAK_BAR_WIDTH, SDLWindow::height, Theme::colors.bgaccent);

  // Draw zero lines in each background rect at barHeight
  Graphics::drawLine(lufsBarX, barHeight, lufsBarX + LUFS_BAR_WIDTH, barHeight, Theme::colors.accent, 1);
  Graphics::drawLine(leftPeakX, barHeight, leftPeakX + PEAK_BAR_WIDTH, barHeight, Theme::colors.accent, 1);
  Graphics::drawLine(rightPeakX, barHeight, rightPeakX + PEAK_BAR_WIDTH, barHeight, Theme::colors.accent, 1);

  // Draw dB labels on the left
  const std::vector<float> labels {0, -6, -12, -24, -48};
  for (const auto& label : labels) {
    // Calculate y position based on dB value (0dB at top, -70dB at bottom)
    float normalizedPos = (label + 70.0f) / 70.0f;
    size_t y = SDLWindow::height - (topHeight + (1.0f - normalizedPos) * barHeight);

    // Draw label (right aligned)
    std::string labelText = std::to_string(static_cast<int>(label));
    auto [w, h] = Graphics::Font::getTextSize(labelText.c_str(), FONT_SIZE_LABELS, window->sdlWindow);
    Graphics::Font::drawText(labelText.c_str(), LABEL_WIDTH - w - 2, y - h / 2, FONT_SIZE_LABELS, Theme::colors.text,
                             window->sdlWindow);

    // Draw horizontal line extending to the peak bars
    Graphics::drawLine(LABEL_WIDTH, y, LABEL_WIDTH + LABEL_LINE_LENGTH, y, Theme::colors.text, 1);
  }

  // Draw peak bars
  if (DSP::Peak::left > 0.0f || DSP::Peak::right > 0.0f) {
    // Convert peak values to dB (amplitude to dB relative to full scale)
    float leftDB = DSP::Peak::left > 0.0f ? 20.0f * log10f(DSP::Peak::left) : -70.0f;
    float rightDB = DSP::Peak::right > 0.0f ? 20.0f * log10f(DSP::Peak::right) : -70.0f;

    // Clamp to -70dB minimum
    float clampedLeftDB = std::max(-70.0f, leftDB);
    float clampedRightDB = std::max(-70.0f, rightDB);

    // Calculate positions for peak bars
    float leftPos = std::max(0.0f, std::min(1.0f, (clampedLeftDB + 70.0f) / 70.0f));
    float rightPos = std::max(0.0f, std::min(1.0f, (clampedRightDB + 70.0f) / 70.0f));

    size_t leftBarHeight = leftPos * barHeight;
    size_t rightBarHeight = rightPos * barHeight;
    size_t leftBarY = SDLWindow::height - (topHeight + barHeight);
    size_t rightBarY = SDLWindow::height - (topHeight + barHeight);

    // Draw left peak bar
    Graphics::drawFilledRect(leftPeakX, leftBarY, PEAK_BAR_WIDTH, leftBarHeight, Theme::colors.color);

    // Draw right peak bar
    Graphics::drawFilledRect(rightPeakX, rightBarY, PEAK_BAR_WIDTH, rightBarHeight, Theme::colors.color);
  }

  // Draw LUFS bar if value is valid
  if (lufs < -70.0f)
    return;

  // Calculate bar height based on LUFS value
  float normalizedPos = std::max(0.0f, (lufs + 70.0f) / 70.0f);
  size_t barFillHeight = normalizedPos * barHeight;
  size_t barFillY = SDLWindow::height - (topHeight + barHeight);

  // Draw the LUFS bar
  Graphics::drawFilledRect(lufsBarX, barFillY, LUFS_BAR_WIDTH, barFillHeight, color);

  // Draw LUFS text label with colored box
  char lufsBuffer[16];
  if (lufs >= -9.9f) {
    snprintf(lufsBuffer, sizeof(lufsBuffer), "%5.2fLUFS", lufs);
  } else {
    snprintf(lufsBuffer, sizeof(lufsBuffer), "%5.1fLUFS", lufs);
  }
  auto [w, h] = Graphics::Font::getTextSize(lufsBuffer, FONT_SIZE_LUFS, window->sdlWindow);

  // Calculate text position based on LUFS bar height, centered on the bar
  size_t textY = barFillY + barFillHeight - static_cast<size_t>(h / 2);

  // Clamp text position to stay within window bounds
  textY = std::max(topHeight, std::min(SDLWindow::height - static_cast<size_t>(h), textY));

  // Draw colored box background
  size_t boxX = lufsBarX + LUFS_BAR_WIDTH + 2;
  size_t boxY = textY - LUFS_TEXT_BOX_VERTICAL_PADDING;
  size_t boxWidth = w + (LUFS_TEXT_BOX_PADDING * 2);
  size_t boxHeight = h + (LUFS_TEXT_BOX_VERTICAL_PADDING * 2);
  Graphics::drawFilledRect(boxX, boxY, boxWidth, boxHeight, color);

  // Draw text
  Graphics::Font::drawText(lufsBuffer, boxX + LUFS_TEXT_BOX_PADDING, textY, FONT_SIZE_LUFS, Theme::colors.background,
                           window->sdlWindow);
}

} // namespace LUFS