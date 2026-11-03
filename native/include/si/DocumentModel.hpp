#pragma once

#include "si/GaussianParser.hpp"
#include "si/SIModel.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <QHash>

namespace si {

enum class DocumentSection {
  AbsoluteEnergy,
  RelativeEnergy,
  SpinDensity,
  SelectedGeometry,
  Other,
  Validation
};

struct DocumentTable {
  DocumentSection section{DocumentSection::Other};
  QString caption;
  QStringList headers;
  QVector<QStringList> rows;
  QVector<int> boldColumns;
  QString stageId;
  int documentNumber{};
  bool hasManualRelativeValues{false};
};

struct CoordinateBlock {
  QString structureName;
  std::vector<Atom> atoms;
  std::optional<int> charge, multiplicity;
  std::optional<double> speEnergy;
};

struct DocumentFigure {
  QByteArray png;
  QStringList structureNames;
  int pixelWidth{}, pixelHeight{};
  QString stageId;
  int documentNumber{};
};

struct SIDocumentModel {
  QString title{"Supporting Information"};
  QString mechanismName;
  QString manuscriptTitle, authors, affiliations, correspondingEmail;
  QString computationalDetails, referencesText;
  QStringList figureCaptions;
  QVector<DocumentFigure> figures;
  QVector<DocumentTable> tables;
  QVector<CoordinateBlock> coordinates;
  int precisionOverride{};
  QStringList stageOrder;
  QHash<QString,QString> stageNames;

  [[nodiscard]] bool empty() const;
  [[nodiscard]] int tableCount() const;
  [[nodiscard]] int rowCount(DocumentSection section) const;
  [[nodiscard]] const DocumentTable* table(DocumentSection section) const;
};

[[nodiscard]] QString computationalDetails(const std::vector<EnergyRecord>& records, EnergyBasis basis, QStringList* warnings=nullptr);
[[nodiscard]] DocumentTable validationTable(const std::vector<EnergyRecord>& records);
[[nodiscard]] QString displayMechanism(const SIDocumentModel& model);
[[nodiscard]] QString relativeGibbsFootnote(const DocumentTable& table);
[[nodiscard]] QString computationalDetailsBody(QString text);
[[nodiscard]] QString formatNumber(double value, int defaultDigits, int precisionOverride);
[[nodiscard]] QString formatNumber(const std::optional<double>& value, int defaultDigits, int precisionOverride);

} // namespace si
