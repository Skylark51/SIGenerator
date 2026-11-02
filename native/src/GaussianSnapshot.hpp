#pragma once

#include "si/GaussianParser.hpp"
#include <QJsonObject>

namespace si {

// A compact, versioned record of all Gaussian values used by SI generation.
// The original .out/.log bytes are never included in a project.
class GaussianSnapshot final {
public:
  [[nodiscard]] static QJsonObject encode(const GaussianResult& result);
  [[nodiscard]] static bool decode(const QJsonObject& snapshot, GaussianResult* result);
};

} // namespace si
