#pragma once
#include "si/DocumentModel.hpp"
#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QVector>

class MoleculeViewWidget;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

// Preparation owns copies of coordinates. The source data used by tables and
// Cartesian Coordinates are never changed by alignment or camera controls.
class FigureSetupDialog final : public QDialog {
  Q_OBJECT
public:
  FigureSetupDialog(const QVector<si::CoordinateBlock>& structures, const QStringList& paths,
                    const QJsonObject& settings, QWidget* parent=nullptr);
  QJsonObject settings() const;
  QVector<si::DocumentFigure> figures() const;
private:
  void showGroup();
  void storeGroup();
  void finish();
  void rebuildDefaultRows();
  QVector<si::CoordinateBlock> structures_;
  QStringList paths_;
  QVector<int> order_;
  QVector<QVector<int>> rows_;
  bool customRows_{};
  QJsonObject views_;
  QJsonObject renderStyle_;
  QHash<QString,int> retainedHydrogens_;
  QVector<si::DocumentFigure> figures_;
  QVector<MoleculeViewWidget*> viewers_;
  QVector<QComboBox*> selectors_;
  QVector<QComboBox*> hydrogenSelectors_;
  QVBoxLayout* panels_{};
  QWidget* panelWidget_{};
  QCheckBox* linked_{};
  QCheckBox* indices_{};
  QCheckBox* hideHydrogens_{};
  QLabel* status_{};
  QPushButton* addPanel_{};
  QPushButton* previous_{};
  QPushButton* next_{};
  int group_{};
  bool changing_{};
};
