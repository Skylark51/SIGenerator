#pragma once

#include "si/DocumentModel.hpp"

#include <QWidget>
#include <QString>

class QTextBrowser;
class QComboBox;
class QPushButton;

class DocumentPreviewWidget final : public QWidget {
  Q_OBJECT
public:
  explicit DocumentPreviewWidget(QWidget* parent = nullptr);
  void setDocumentModel(const si::SIDocumentModel& model);
private:
  void renderPreview();
  void copySelectedTable();
  void copySelectedTableImage();
  si::SIDocumentModel model_;
  QComboBox* tableSelector_{};
  QPushButton* copyButton_{};
  QPushButton* copyImageButton_{};
  QTextBrowser* preview_{};
  QString lastHtml_;
};
