#pragma once
#include <QString>
#include <QVector>

namespace si {
struct NamedStructureInput {
  QString original;
  QString fileName;
  QString label;
  bool implicitPair{};
};
// Spreadsheet label<TAB>filename rows, shorthand label filename lines, ordinary
// file-name cells and explicit (label filename) pairs may be mixed.
QVector<NamedStructureInput> namedStructureInputs(const QString& text);
// Idempotent markup, shared by tables, figures, coordinate DOCX and XYZ names.
QString structureLabelMarkup(QString label);
QString formattedTextHtml(const QString& text);
}
