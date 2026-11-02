#pragma once
#include "si/DocumentModel.hpp"

namespace si {
class XyzWriter final {
public:
  static bool write(const QString& path, const SIDocumentModel& model, QString* error);
};
} // namespace si
