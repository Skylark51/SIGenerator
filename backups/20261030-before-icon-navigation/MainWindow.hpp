#pragma once

#include "si/DocumentModel.hpp"
#include "si/GaussianParser.hpp"
#include "si/SIModel.hpp"

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <memory>

class DocumentPreviewWidget;
class MoleculeViewWidget;
class QCheckBox;
class QComboBox;
class QFutureWatcherBase;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QTimer;

class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  MainWindow();
  ~MainWindow() override;

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  friend struct Phase1Smoke;
  QWidget* createInputPanel();
  QWidget* createWorkspace();
  QWidget* createQuickToolbar();
  void connectPreviewInputs();
  void chooseFiles();
  void chooseDirectory();
  void addNamedFiles(bool replace=false);
  void acceptPaths(const QStringList& paths);
  void parsePaths(const QList<QString>& paths, bool append = false, bool discoverIrc = true);
  void selectStructure();
  void renameSelected();
  void removeSelected();
  void moveSelectedStructure(int targetRow);
  void saveProject();
  void openProject();
  void exportSiDocument();
  bool prepareFigures();
  QJsonObject figureSettings_;
  QVector<si::DocumentFigure> preparedFigures_;
  QByteArray figureSignature_;
  void exportXyzDocument();
  void exportCoordinatesDocument();
  void syncReferenceOptions();
  void refreshPreflight();
  [[nodiscard]] QStringList preflightErrors() const;
  [[nodiscard]] bool validateExport();
  [[nodiscard]] std::vector<si::EnergyRecord> energyRecords() const;
  void scheduleDocumentRefresh();
  void refreshDocumentModel();
  [[nodiscard]] si::SIDocumentModel buildDocumentModel();
  [[nodiscard]] QStringList structureLabels() const;
  [[nodiscard]] QStringList sourcePaths() const;
  void updateGeometryFeedback(const QVector<int>& atomIds);
  void appendSelectedGeometry();
  void scrollTo(QWidget* target);

  QScrollArea* inputScroll_{};
  QLineEdit* baseDirectory_{};
  QTextEdit* fileNames_{};
  QLabel* fileNamesInfo_{};
  QTableWidget* files_{};
  QLabel* status_{};
  QLineEdit* structureLabel_{};
  QComboBox* reference_{};
  QString pendingReferencePath_;
  bool parsing_{};
  quint64 importGeneration_{};
  QFutureWatcherBase* importWatcher_{};
  struct ImportRequest { QList<QString> paths; bool discoverIrc{true}; };
  QList<ImportRequest> queuedImports_;
  QComboBox* referenceMode_{};
  QLineEdit* manualReference_{};
  QComboBox* energyBasis_{};
  QComboBox* energyInputMode_{};
  QTextEdit* manualAbsoluteNames_{};
  QTextEdit* manualAbsoluteValues_{};
  QTextEdit* manualRelativeNames_{};
  QTextEdit* manualRelativeValues_{};
  QTextEdit* spinIndices_{};
  QTextEdit* spinNames_{};
  QTextEdit* geometryIndices_{};
  QLabel* geometryAutoInfo_{};
  QTextEdit* geometryNames_{};
  QTextEdit* captions_{};
  QLineEdit* mechanismName_{};
  QLineEdit* documentTitle_{};
  QLineEdit* manuscriptTitle_{};
  QLineEdit* authors_{};
  QLineEdit* affiliations_{};
  QLineEdit* correspondingEmail_{};
  QTextEdit* computationalDetails_{};
  QTextEdit* referencesText_{};
  bool detailsEdited_{};
  QLineEdit* siOutputPath_{};
  QLineEdit* xyzOutputPath_{};
  QCheckBox* includeAbsolute_{};
  QCheckBox* includeDispersion_{};
  QCheckBox* includeComplexation_{};
  QSpinBox* decimalPlaces_{};
  QLabel* interactionInfo_{};
  QWidget* energySection_{};
  QWidget* spinSection_{};
  QWidget* geometrySection_{};
  MoleculeViewWidget* moleculeView_{};
  DocumentPreviewWidget* preview_{};
  QTimer* previewTimer_{};
  si::SIDocumentModel documentModel_;
  std::shared_ptr<si::GaussianCache> cache_{std::make_shared<si::GaussianCache>()};
  QHash<QString,QString> pairedPaths_;
  QHash<QString,std::shared_ptr<const si::GaussianResult>> parsed_;
  QStringList pendingStructureLabels_;
  QHash<QString,QString> pendingLabelsByPath_;
  QHash<QString,QString> namedImportLabels_;
};
