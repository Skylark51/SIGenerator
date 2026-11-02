#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
struct ProjectState {
  QString projectName;
  QStringList files;
  QString structureNames;
  QString referenceMode{"single"};
  QString referencePath;
  QString manualReference;
  QString energyInputMode{"automatic"};
  QString energyBasis{"0"};
  QString manualAbsoluteNames;
  QString manualAbsoluteValues;
  QString manualRelativeNames;
  QString manualRelativeValues;
  QString spinNames;
  QString spinIndices;
  QString geometryNames;
  QString geometryIndices;
  QString captions;
  QJsonObject figureSettings;
  QJsonArray reactionStages;
  QString mechanismName;
  QString documentTitle{"Supporting Information"};
  QString manuscriptTitle, authors, affiliations, correspondingEmail;
  QString computationalDetails, referencesText;
  bool computationalDetailsEdited{};
  bool includeAbsolute{false};
  int decimalPlaces{};
  bool includeDispersion{false};
  bool includeComplexation{false};
  QString baseDirectory;
  QString siPath;
  QString xyzPath;
};
class ProjectStore final { public: static bool save(const QString& path, const ProjectState& state, QString* error); static bool load(const QString& path, ProjectState* state, QString* error); };
