#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct ProjectVersion {
  QString id;
  QDateTime created;
  QString message, docxPath;
};

struct ProjectChange {
  QString category, item;
  QJsonValue before, after;
};

class ProjectHistory final {
public:
  explicit ProjectHistory(QString projectPath);
  bool record(const QJsonObject& project,const QString& message,QString* error,const QString& docx={}) const;
  bool list(QVector<ProjectVersion>* versions,QString* error) const;
  bool read(const QString& id,QJsonObject* project,QString* error) const;
  bool restore(const QString& id,const QJsonObject& current,QString* error) const;
  static QVector<ProjectChange> compare(const QJsonObject& before,const QJsonObject& after);

private:
  bool recordLocked(const QJsonObject& project,const QString& message,QString* error,const QString& docx={}) const;
  QString projectPath_, directory_;
};
