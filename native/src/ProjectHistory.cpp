#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace {
QString digest(const QJsonObject& object){
  return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                                     QCryptographicHash::Sha256).toHex());
}
bool fail(QString* error,const QString& message){
  if(error)*error=message;
  return false;
}
bool readObject(const QString& path,QJsonObject* object,QString* error){
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly))return fail(error,file.errorString()+": "+path);
  const auto document=QJsonDocument::fromJson(file.readAll());
  if(!document.isObject())return fail(error,QStringLiteral("Invalid history JSON: ")+path);
  *object=document.object();
  return true;
}
bool validDigest(const QString& id){
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
  return pattern.match(id).hasMatch();
}
bool validId(const QString& id){
  const auto uuid=QUuid::fromString(id);
  return !uuid.isNull()&&uuid.toString(QUuid::WithoutBraces)==id;
}
QJsonObject keyed(const QJsonArray& rows,const QString& key,const QString& fallback){
  QJsonObject result;
  for(const auto& row:rows){
    const auto object=row.toObject();
    const auto id=object.value(key).toString(object.value(fallback).toString());
    if(!id.isEmpty())result.insert(id,object);
  }
  return result;
}
QJsonArray order(const QJsonArray& rows,const QString& key,const QString& fallback){
  QJsonArray result;
  for(const auto& row:rows){
    const auto object=row.toObject();
    result.append(object.value(key).toString(object.value(fallback).toString()));
  }
  return result;
}
}

ProjectHistory::ProjectHistory(QString projectPath)
  :projectPath_(QFileInfo(projectPath).absoluteFilePath()),directory_(projectPath_+".history"){}

bool ProjectHistory::list(QVector<ProjectVersion>* versions,QString* error) const{
  versions->clear();
  const QDir entries(QDir(directory_).filePath("versions"));
  for(const auto& file:entries.entryInfoList({"*.json"},QDir::Files)){
    QJsonObject entry;
    if(!readObject(file.absoluteFilePath(),&entry,error))return false;
    const auto created=QDateTime::fromString(entry.value("created").toString(),Qt::ISODateWithMs);
    if(!created.isValid()||!validId(file.completeBaseName())||!validDigest(entry.value("project").toString()))
      return fail(error,QStringLiteral("Invalid version entry: ")+file.fileName());
    versions->append({file.completeBaseName(),created,entry.value("message").toString()});
  }
  std::sort(versions->begin(),versions->end(),[](const auto& a,const auto& b){
    return a.created==b.created?a.id>b.id:a.created>b.created;
  });
  return true;
}

bool ProjectHistory::record(const QJsonObject& project,const QString& message,QString* error) const{
  if(!QDir().mkpath(directory_))return fail(error,QStringLiteral("Cannot create history folder: ")+directory_);
  QLockFile lock(QDir(directory_).filePath("write.lock"));
  if(!lock.tryLock(0))return fail(error,QStringLiteral("Project history is locked."));
  return recordLocked(project,message,error);
}

bool ProjectHistory::recordLocked(const QJsonObject& project,const QString& message,QString* error) const{
  const QDir folder(directory_);
  if(!folder.mkpath("objects")||!folder.mkpath("versions"))
    return fail(error,QStringLiteral("Cannot create history storage."));
  auto saveObject=[&](const QJsonObject& object){
    const auto hash=digest(object);
    const auto path=folder.filePath("objects/"+hash+".json");
    if(QFileInfo::exists(path)){
      QJsonObject existing;
      if(!readObject(path,&existing,error))return QString();
      if(digest(existing)!=hash){
        fail(error,QStringLiteral("History object checksum mismatch: ")+hash);
        return QString();
      }
    }else if(!ProjectStore::write(path,object,error))return QString();
    return hash;
  };
  auto packed=project;
  auto snapshots=project.value("gaussian_snapshots").toObject();
  for(auto it=snapshots.begin();it!=snapshots.end();++it){
    if(!it.value().isObject())return fail(error,QStringLiteral("Invalid Gaussian snapshot."));
    const auto hash=saveObject(it.value().toObject());
    if(hash.isEmpty())return false;
    it.value()=hash;
  }
  packed.insert("gaussian_snapshots",snapshots);
  const auto hash=saveObject(packed);
  if(hash.isEmpty())return false;
  const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
  return ProjectStore::write(folder.filePath("versions/"+id+".json"),
    {{"created",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"message",message},{"project",hash}},error);
}

bool ProjectHistory::read(const QString& id,QJsonObject* project,QString* error) const{
  if(!validId(id))return fail(error,QStringLiteral("Invalid version ID."));
  const QDir folder(directory_);
  auto loadObject=[&](const QString& hash,QJsonObject* object){
    if(!validDigest(hash))return fail(error,QStringLiteral("Invalid history object ID."));
    if(!readObject(folder.filePath("objects/"+hash+".json"),object,error))return false;
    return digest(*object)==hash||fail(error,QStringLiteral("History object checksum mismatch: ")+hash);
  };
  QJsonObject entry,packed;
  if(!readObject(folder.filePath("versions/"+id+".json"),&entry,error)||
     !loadObject(entry.value("project").toString(),&packed))return false;
  auto snapshots=packed.value("gaussian_snapshots").toObject();
  for(auto it=snapshots.begin();it!=snapshots.end();++it){
    QJsonObject snapshot;
    if(!loadObject(it.value().toString(),&snapshot))return false;
    it.value()=snapshot;
  }
  packed.insert("gaussian_snapshots",snapshots);
  *project=std::move(packed);
  return true;
}

bool ProjectHistory::restore(const QString& id,const QJsonObject& current,QString* error) const{
  QLockFile lock(QDir(directory_).filePath("write.lock"));
  if(!lock.tryLock(0))return fail(error,QStringLiteral("Project history is locked."));
  QJsonObject project;
  return read(id,&project,error)&&recordLocked(current,QStringLiteral("Before restore"),error)&&
    ProjectStore::write(projectPath_,project,error);
}

QVector<ProjectChange> ProjectHistory::compare(const QJsonObject& before,const QJsonObject& after){
  QVector<ProjectChange> changes;
  auto change=[&](const QString& category,const QString& item,const QJsonValue& a,const QJsonValue& b){
    if(a!=b)changes.append({category,item,a,b});
  };
  auto compareKeys=[](const QJsonObject& a,const QJsonObject& b,const auto& compare){
    auto keys=a.keys()+b.keys();
    keys.removeDuplicates();
    keys.sort();
    for(const auto& key:keys)compare(key,a.value(key),b.value(key));
  };
  const auto oldStructures=before.value("structures").toArray(),newStructures=after.value("structures").toArray();
  compareKeys(keyed(oldStructures,"main_file","gaussian_file_path"),keyed(newStructures,"main_file","gaussian_file_path"),
    [&](const auto& path,const auto& a,const auto& b){
      const auto oldName=a.toObject().value("structure_name"),newName=b.toObject().value("structure_name");
      if(a.isUndefined())change("Structure added",path,a,newName);
      else if(b.isUndefined())change("Structure removed",path,oldName,b);
      else change("Structure renamed",path,oldName,newName);
    });
  const auto oldOrder=order(oldStructures,"main_file","gaussian_file_path"),newOrder=order(newStructures,"main_file","gaussian_file_path");
  if(oldOrder.size()==newOrder.size())change("Structures","Order",oldOrder,newOrder);
  const auto oldStages=before.value("reaction_stages").toArray(),newStages=after.value("reaction_stages").toArray();
  change("Reaction stage","Order",order(oldStages,"id","name"),order(newStages,"id","name"));
  compareKeys(keyed(oldStages,"id","name"),keyed(newStages,"id","name"),[&](const auto& id,const auto& a,const auto& b){
    const auto oldStage=a.toObject(),newStage=b.toObject();
    const auto name=newStage.value("name").toString(oldStage.value("name").toString(id));
    compareKeys(oldStage,newStage,[&](const auto& key,const auto& oldValue,const auto& newValue){
      const auto category=key.startsWith("figure")?"Figure":
        (key=="reference"||key=="absolute"||key=="relative"||key.contains("relative_"))?"Energy":"Reaction stage";
      change(category,name+" / "+key,oldValue,newValue);
    });
  });
  compareKeys(before,after,[&](const auto& key,const auto& a,const auto& b){
    if(key=="structures"||key=="reaction_stages"||key=="generation_history"||key=="version")return;
    if(key=="gaussian_snapshots"){
      compareKeys(a.toObject(),b.toObject(),[&](const auto& path,const auto& x,const auto& y){
        if(x!=y)change("Gaussian data",path,x.isObject()?QJsonValue(digest(x.toObject())):x,
                                                y.isObject()?QJsonValue(digest(y.toObject())):y);
      });
    }else if(key=="molecular_figures"||key=="figure_captions"||key=="captions")change("Figure",key,a,b);
    else if(key.contains("energy")||key=="reference_settings"||key.startsWith("include_")||key=="decimal_places")
      change("Energy",key,a,b);
    else change(key.contains("paired")?"SPE":"Project",key,a,b);
  });
  return changes;
}
