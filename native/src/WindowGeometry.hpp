#pragma once

#include <QScreen>
#include <QWidget>
#include <algorithm>

// Include the native title bar and borders, and leave space for the taskbar.
inline void fitWindowToScreen(QWidget* window) {
  if (window->isMaximized() || window->isFullScreen() || !window->screen()) return;
  const auto available = window->screen()->availableGeometry().adjusted(8, 8, -8, -8);
  const auto frame = window->frameGeometry();
  const int borderWidth = frame.width() - window->width();
  const int borderHeight = frame.height() - window->height();
  window->resize(std::min(window->width(), available.width() - borderWidth),
                 std::min(window->height(), available.height() - borderHeight));
  const auto resizedFrame = window->frameGeometry();
  window->move(std::clamp(resizedFrame.x(), available.left(),
                         std::max(available.left(), available.right() - resizedFrame.width() + 1)),
               std::clamp(resizedFrame.y(), available.top(),
                         std::max(available.top(), available.bottom() - resizedFrame.height() + 1)));
}
