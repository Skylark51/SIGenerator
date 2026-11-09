#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

int main(){
  QTemporaryDir temp;
  if(!temp.isValid())return 1;
  const auto path=temp.filePath("history.si_project.json");
  ProjectHistory history(path);
  QString error;
  auto check=[&](bool passed,const char* label){
    if(!passed)std::cerr<<label<<": "<<error.toStdString()<<'\n';
    return passed;
  };
  QVector<ProjectVersion> versions;
  if(!check(history.list(&versions,&error)&&versions.isEmpty(),"legacy project has no sidecar requirement"))return 1;
  ProjectState initial;
  initial.files={temp.filePath("first.out"),temp.filePath("removed.out")};
  initial.structureNames="Original\nRemoved";
  initial.gaussianSnapshots.insert(initial.files.front(),QJsonObject{{"synthetic",true}});
  const auto first=ProjectStore::serialize(path,initial);
  if(!check(history.record(first,"Initial",&error),"create version"))return 1;
  if(!check(history.list(&versions,&error)&&versions.size()==1,"list initial version"))return 1;
  const auto firstId=versions.front().id;
  auto edited=initial;
  edited.files[1]=temp.filePath("added.out");
  edited.structureNames="Renamed\nAdded";
  edited.energyBasis="2";
  edited.figureSettings={{"layout",2}};
  const auto second=ProjectStore::serialize(path,edited);
  const auto output=temp.filePath("SI.docx");
  const QByteArray original("original DOCX bytes");
  auto writeOutput=[&](const QByteArray& bytes){
    QFile file(output);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
  };
  if(!check(writeOutput(original)&&history.record(second,"Edited",&error,output)&&writeOutput("new export"),
            "archive DOCX before the output is overwritten"))return 1;
  if(!check(history.list(&versions,&error)&&versions.size()==2,"list archived version"))return 1;
  const auto archived=std::find_if(versions.begin(),versions.end(),[](const auto& version){return version.message=="Edited";});
  if(!check(archived!=versions.end()&&!archived->docxPath.isEmpty(),"archived version exposes its DOCX"))return 1;
  QFile archive(archived->docxPath);
  if(!check(archive.open(QIODevice::ReadOnly)&&archive.readAll()==original,"archive preserves original bytes"))return 1;
  const QDir objects(path+".history/objects");
  if(!check(objects.entryList({"*.json"},QDir::Files).size()==3,"unchanged extraction blob stored once"))return 1;
  QJsonObject reconstructed;
  if(!check(history.read(firstId,&reconstructed,&error)&&first==reconstructed,"snapshot round trip"))return 1;
  const auto changes=ProjectHistory::compare(first,second);
  for(const auto category:{"Structure added","Structure removed","Structure renamed","Energy","Figure"}){
    if(!check(std::any_of(changes.begin(),changes.end(),[category](const auto& change){return change.category==category;}),
              category))return 1;
  }
  if(!check(!history.restore("../invalid",second,&error),"invalid version ID rejected"))return 1;
  if(!check(history.restore(firstId,second,&error),"restore creates a recovery version"))return 1;
  ProjectState restored;
  if(!check(ProjectStore::load(path,&restored,&error)&&restored.files==initial.files&&
            restored.structureNames==initial.structureNames,"restored relative paths resolve in original folder"))return 1;
  if(!check(history.list(&versions,&error)&&versions.size()==3,"recovery version appended"))return 1;
  const auto recovery=std::find_if(versions.begin(),versions.end(),[](const auto& v){return v.message=="Before restore";});
  return check(recovery!=versions.end()&&history.read(recovery->id,&reconstructed,&error)&&reconstructed==second,
               "pre-restore working state recoverable")?0:1;
}
