#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>

// Read-only import. Youngcraft's settings are never changed by SI Generator.
QJsonObject currentYoungcraftStyle();
QColor molecularElementColor(int atomicNumber, const QJsonObject& style);
QImage molecularSphere(const QColor& color, const QJsonObject& style);
