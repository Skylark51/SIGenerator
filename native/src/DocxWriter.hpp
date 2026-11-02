#pragma once

#include "si/DocumentModel.hpp"
#include <QString>
#include <QStringList>
#include <QVector>

class DocxWriter final {
public:
  struct Table { QString title; QStringList headers; QVector<QStringList> rows; };
  static bool writeDocument(const QString& path, const si::SIDocumentModel& model, QString* error);
  static bool writeCoordinates(const QString& path, const si::SIDocumentModel& model, QString* error);
  static bool write(const QString& path, const QString& title, const QStringList& headers, const QVector<QStringList>& rows, QString* error);
  static bool writeTables(const QString& path, const QString& title, const QVector<Table>& tables, QString* error);
};
