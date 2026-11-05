#pragma once

#include <QRegularExpression>
#include <QStringList>

inline QStringList lines(const QString& text){return text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),Qt::SkipEmptyParts);}
