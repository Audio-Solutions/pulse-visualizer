#include "include/config.hpp"
#include "include/dsp.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"
#include "include/window_manager.hpp"

namespace VU {

WindowManager::VisualizerWindow* window;

// Layout configuration constants
constexpr size_t LABEL_WIDTH = 25;
constexpr size_t LABEL_GAP = 10;
constexpr size_t VU_BAR_WIDTH = 10;
constexpr size_t LABEL_LINE_LENGTH = 5;
constexpr size_t TOP_HEIGHT_PERCENT = 10;
constexpr size_t FONT_SIZE_LABELS = 10;

float scaleDB(float db) {
  float min_db = -20.0f;
  float max_db = 3.0f;
  float a = 1.5f; // curve strength

  if (Config::options.vu.scale == "log") {
    float M = std::max(std::abs(min_db), max_db);

    auto sign = [](float x) -> float { return (x > 0) - (x < 0); };

    auto curve = [&](float x) -> float {
      float absx = std::abs(x);
      float exponent = 1.0f / a;
      float val = (std::pow(absx, exponent) - 0.0f) / (std::pow(M, exponent) - 0.0f);
      return sign(x) * val;
    };

    float g_min = curve(min_db);
    float g_max = curve(max_db);

    float g_db = curve(db);

    return (g_db - g_min) / (g_max - g_min);
  } else {
    return std::max(0.0f, (db - min_db) / (max_db - min_db));
  }
}

void render() {
  if (!window)
    return;

  WindowManager::setViewport(window->x, window->width, SDLWindow::height);

  float dB = 20.0f * log10(DSP::RMS::rms) + Config::options.vu.calibration_db;

  if (Config::options.vu.style == "digital") {
    // Calculate layout positions
    const size_t topHeight = SDLWindow::height * (TOP_HEIGHT_PERCENT / 100.0f);
    const size_t barHeight = SDLWindow::height - topHeight;

    // VU bar position
    const size_t vuBarX = LABEL_WIDTH + LABEL_GAP;

    // Draw background bar
    Graphics::drawFilledRect(vuBarX, 0, VU_BAR_WIDTH, SDLWindow::height - topHeight, Theme::colors.bgaccent);

    // Draw zero line in background rect at 0dB
    float zeroLineY = SDLWindow::height - (topHeight + (1.0f - scaleDB(0.0f)) * barHeight);
    Graphics::drawLine(vuBarX, zeroLineY, vuBarX + VU_BAR_WIDTH, zeroLineY, Theme::colors.accent, 1);

    // Draw dB labels on the left
    const std::vector<float> linLabels {3, 0, -3, -6, -12, -18};
    const std::vector<float> logLabels {3, 1, 0, -1, -3, -6, -9, -12, -18};
    const std::vector<float>& labels = Config::options.vu.scale == "log" ? logLabels : linLabels;

    for (const auto& label : labels) {
      // Calculate y position based on dB value
      float normalizedPos = scaleDB(label);
      size_t y = SDLWindow::height - (topHeight + (1.0f - normalizedPos) * barHeight);

      // Draw label (right aligned)
      std::string labelText = std::to_string(static_cast<int>(label));
      auto [w, h] = Graphics::Font::getTextSize(labelText.c_str(), FONT_SIZE_LABELS, window->sdlWindow);
      Graphics::Font::drawText(labelText.c_str(), LABEL_WIDTH - w - 2, y - h / 2, FONT_SIZE_LABELS, Theme::colors.text,
                               window->sdlWindow);

      // Draw horizontal line extending to the VU bar
      Graphics::drawLine(LABEL_WIDTH, y, LABEL_WIDTH + LABEL_LINE_LENGTH, y, Theme::colors.text, 1);
    }

    // Draw VU bar if value is valid
    if (dB < -20.0f)
      return;

    // Calculate bar height based on dB value
    float normalizedPos = scaleDB(dB);
    size_t barFillHeight = normalizedPos * barHeight;
    size_t barFillY = SDLWindow::height - (topHeight + barHeight);

    // Draw the VU bar with colored segments
    const float* mainColor = Theme::colors.vu_main[3] > 1e-6f ? Theme::colors.vu_main : Theme::colors.color;
    const float* clipColor = Theme::colors.vu_clip[3] > 1e-6f ? Theme::colors.vu_clip : Theme::colors.color;
    const float* cautionColor = Theme::colors.vu_caution[3] > 1e-6f ? Theme::colors.vu_caution : Theme::colors.color;

    // Draw bottom segment (0 to -3dB)
    size_t bottomSegmentHeight = std::min(static_cast<float>(barFillHeight), scaleDB(-3.0f) * barHeight);
    Graphics::drawFilledRect(vuBarX, barFillY, VU_BAR_WIDTH, bottomSegmentHeight, mainColor);

    // Draw middle segment (-3dB to 0dB) only if volume is above -3dB
    if (dB >= -3.0f) {
      size_t middleSegmentStart = std::min(static_cast<float>(barFillHeight), scaleDB(-3.0f) * barHeight);
      size_t middleSegmentEnd = std::min(static_cast<float>(barFillHeight), scaleDB(0.0f) * barHeight);
      if (middleSegmentEnd > middleSegmentStart) {
        Graphics::drawFilledRect(vuBarX, barFillY + middleSegmentStart, VU_BAR_WIDTH,
                                 middleSegmentEnd - middleSegmentStart, cautionColor);
      }
    }

    // Draw top segment (0dB to +3dB) only if volume is above 0dB
    if (dB >= 0.0f) {
      size_t topSegmentStart = std::min(static_cast<float>(barFillHeight), scaleDB(0.0f) * barHeight);
      size_t topSegmentEnd = std::min(static_cast<float>(barFillHeight), scaleDB(3.0f) * barHeight);
      if (topSegmentEnd > topSegmentStart) {
        Graphics::drawFilledRect(vuBarX, barFillY + topSegmentStart, VU_BAR_WIDTH, topSegmentEnd - topSegmentStart,
                                 clipColor);
      }
    }
  } else {
    float x0 = window->width / 2;
    float y0 = -SDLWindow::height / 3;

    // clamp dB to minmax range
    dB = std::clamp(dB, -20.0f, 3.0f);

    auto toAngle = [](float db) {
      // Use the same scaleDB function for consistent mapping
      float normalizedPos = scaleDB(db);
      return -90.0f * normalizedPos + 135.0f;
    };

    // map dB to angle in degrees
    float targetAngle = toAngle(dB);

    static float currentAngle = targetAngle;
    static float velocity = 0.0f;
    static float lastTime = 0.0f;
    float currentTime = SDL_GetTicks() / 1000.0f;
    float deltaTime = lastTime > 0.0f ? currentTime - lastTime : 0.016f;
    lastTime = currentTime;

    if (Config::options.vu.enable_momentum) {
      // Spring physics simulation
      float springForce = (targetAngle - currentAngle) * Config::options.vu.spring_constant;
      float dampingForce = -velocity * Config::options.vu.damping_ratio;
      float acceleration = springForce + dampingForce;

      velocity += acceleration * deltaTime;
      currentAngle += velocity * deltaTime;
    } else {
      currentAngle = targetAngle;
      velocity = 0.0f;
    }

    // draw arcs
    Graphics::drawArc(x0, y0, SDLWindow::height, toAngle(-20.0f), toAngle(0.0), Theme::colors.accent, 5, 100);
    Graphics::drawArc(x0, y0, SDLWindow::height, toAngle(0.0f), toAngle(3.0f), Theme::colors.vu_clip, 5, 100);

    // draw label lines perpendicular to the arc
    const std::vector<float> linLabels {3, 0, -3, -6, -12, -20};
    const std::vector<float> logLabels {3, 1, 0, -1, -3, -6, -9, -12, -15, -20};
    const std::vector<float>& labels = Config::options.vu.scale == "log" ? logLabels : linLabels;
    for (const auto& label : labels) {
      float angle = toAngle(label);

      float xArc = x0 + (SDLWindow::height - 2.5f) * cos(angle * M_PI / 180.0f);
      float yArc = y0 + (SDLWindow::height - 2.5f) * sin(angle * M_PI / 180.0f);

      // point outside the arc
      float x1 = x0 + (SDLWindow::height * 1.1f) * cos(angle * M_PI / 180.0f);
      float y1 = y0 + (SDLWindow::height * 1.1f) * sin(angle * M_PI / 180.0f);

      // point 12px further
      float x2 = x1 + 12 * cos(angle * M_PI / 180.0f);
      float y2 = y1 + 12 * sin(angle * M_PI / 180.0f);

      float* color = label > 0.0f ? Theme::colors.vu_clip : Theme::colors.accent;

      // draw line from arc to outside point
      Graphics::drawLine(xArc, yArc, x1, y1, color, 2);

      // draw centered text at x2, y2
      std::string labelText = std::to_string(static_cast<int>(label));
      auto [w, h] = Graphics::Font::getTextSize(labelText.c_str(), FONT_SIZE_LABELS + 2, window->sdlWindow);
      Graphics::Font::drawText(labelText.c_str(), x2 - w / 2, y2 - h / 2, FONT_SIZE_LABELS + 2,
                               label > 0.0f ? color : Theme::colors.text, window->sdlWindow);
    }

    float length = SDLWindow::height * 1.1f;

    // map x1 and y1
    float x1 = x0 + length * cos(currentAngle * M_PI / 180.0f);
    float y1 = y0 + length * sin(currentAngle * M_PI / 180.0f);

    float* color = Theme::colors.color;
    if (Theme::colors.vu_main[3] > 1e-6f)
      color = Theme::colors.vu_main;

    // draw line from intersection at y = 0 to x1, y1
    Graphics::drawLine(x0 + (-y0 / (y1 - y0)) * (x1 - x0), 0, x1, y1, color, Config::options.vu.needle_width);
  }
}

} // namespace VU