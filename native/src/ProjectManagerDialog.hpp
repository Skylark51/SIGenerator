#pragma once
#include <QDialog>

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;

class ProjectManagerDialog final : public QDialog {
public:
  explicit ProjectManagerDialog(QWidget* parent=nullptr);
  void refreshProjects();
  static void rememberProject(const QString& path);
  QString selectedProject() const { return selectedPath_; }
protected:
  void showEvent(QShowEvent* event) override;
private:
  void openSelected();
  void createProject();
  void chooseExisting();
  bool selectProject(const QString& path);
  void renderProjects(const QString& preferredPath = {});
  void applyTheme(int index);
  QListWidget* projects_{};
  QLineEdit* name_{};
  QLineEdit* location_{};
  QPushButton* open_{};
  QPushButton* previous_{};
  QPushButton* next_{};
  QLabel* pageLabel_{};
  int page_{};
  QString selectedPath_;
};
