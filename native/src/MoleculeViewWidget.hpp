#pragma once

#include "si/GaussianParser.hpp"

#include <QOpenGLWidget>
#include <QPointF>
#include <QVector>
#include <QImage>
#include <QJsonObject>
#include <QHash>

class QPainter;

class MoleculeViewWidget final : public QOpenGLWidget {
  Q_OBJECT
public:
  enum class InteractionMode { View, SpinDensity, Geometry };

  explicit MoleculeViewWidget(QWidget* parent = nullptr);
  void setMolecule(const std::vector<si::Atom>& atoms, const std::vector<si::SpinDensity>& spins, const std::string& spinBasisLevel="unknown", bool spinFallback=false);
  void clearMolecule();
  void setInteractionMode(InteractionMode mode);
  [[nodiscard]] InteractionMode interactionMode() const { return mode_; }
  void setAtomIndicesVisible(bool visible);
  [[nodiscard]] bool atomIndicesVisible() const { return showIndices_; }
  void setHydrogenVisibility(bool hideHydrogens, int retainedHydrogenIndex = 0);
  void resetView();
  void setCaptureFraming(bool enabled) { captureFraming_=enabled;update(); }
  void setRenderStyle(const QJsonObject& style);
  QJsonObject renderStyle() const { return renderStyle_; }
  QJsonObject cameraState() const;
  void setCameraState(const QJsonObject& state);
  QImage renderMolecule(QSize size = QSize(1200, 1000)) const;
  void clearSelection();
  [[nodiscard]] QVector<int> selectedAtoms() const { return selectedAtoms_; }

signals:
  void atomPicked(int atomIndex, const QString& element, double spinDensity, bool hasSpinDensity);
  void geometrySelectionChanged(const QVector<int>& atomIndices);
  void cameraChanged();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  struct ProjectedAtom { QPointF point; double depth{}; double radius{}; int index{}; };
  struct Bond { int first{}; int second{}; };
  [[nodiscard]] QVector<ProjectedAtom> projectedAtoms(QSize size) const;
  [[nodiscard]] QVector<ProjectedAtom> projectedAtoms() const;
  QRect captureRect() const;
  double atomRadius(const si::Atom& atom) const;
  void paintScene(QPainter& painter, QSize size, bool clean) const;
  [[nodiscard]] bool atomVisible(int atomIndex) const;
  [[nodiscard]] int pickAtom(const QPointF& position) const;
  void rebuildBonds();
  void handleClick(const QPointF& position);

  QJsonObject renderStyle_;
  mutable QHash<int,QImage> sphereCache_;
  std::vector<si::Atom> atoms_;
  std::vector<si::SpinDensity> spins_;
  std::string spinBasisLevel_{"unknown"};
  bool spinFallback_{};
  QVector<Bond> bonds_;
  QVector<int> selectedAtoms_;
  InteractionMode mode_{InteractionMode::View};
  bool showIndices_{true};
  bool hideHydrogens_{};
  int retainedHydrogenIndex_{};
  bool captureFraming_{};
  QPoint lastMouse_;
  bool dragging_{};
  bool moved_{};
  Qt::MouseButton dragButton_{Qt::NoButton};
  double yaw_{-25.0};
  double pitch_{20.0};
  double roll_{};
  double zoom_{1.0};
  QPointF pan_;
};
