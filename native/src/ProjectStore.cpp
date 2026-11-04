#include "ProjectStore.hpp"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace {
QJsonValue portablePaths(const QJsonValue& value,const QDir& folder,bool loading) {
  if(value.isArray()){
    QJsonArray output;
    for(const auto& child:value.toArray())output.append(portablePaths(child,folder,loading));
    return output;
  }
  if(value.isObject()){
    QJsonObject output;
    const auto object=value.toObject();
    for(auto it=object.begin();it!=object.end();++it){
      const auto converted=portablePaths(QJsonValue(it.key()),folder,loading);
      output.insert(converted.toString(it.key()),portablePaths(it.value(),folder,loading));
    }
    return output;
  }
  if(!value.isString())return value;
  const QString input=QDir::fromNativeSeparators(value.toString());
  if(input.isEmpty())return value;
  if(loading){
    const QString suffix=QFileInfo(input).suffix().toLower();
    if(!QFileInfo(input).isAbsolute()&&(suffix=="out"||suffix=="log"))
      return QDir::cleanPath(folder.absoluteFilePath(input));
    return value;
  }
  if(!QFileInfo(input).isAbsolute())return value;
  const auto projectPrefix=QDir::cleanPath(folder.absolutePath())+"/";
  if(!input.startsWith(projectPrefix,Qt::CaseInsensitive))return value;
  return folder.relativeFilePath(input);
}
}

bool ProjectStore::save(const QString& path, const ProjectState& state, QString* error) {
  const auto names=state.structureNames.split(QRegularExpression("[\\r\\n]+"),Qt::SkipEmptyParts);QJsonArray structures;
  for(qsizetype index=0;index<state.files.size();++index){const auto& file=state.files[index];const auto name=index<names.size()?names[index].trimmed():QFileInfo(file).completeBaseName();structures.append(QJsonObject{{"enabled",true},{"structure_name",name},{"main_file",file},{"gaussian_file_path",file}});}
  QJsonObject object{{"version",9},{"project_name",state.projectName},{"reaction_stages",state.reactionStages},{"molecular_figures",state.figureSettings},{"structures",structures},{"reference_settings",QJsonObject{{"mode",state.referenceMode},{"energy_basis",state.energyBasis},{"single_reference_path",state.referencePath},{"reference_path",state.referencePath},{"manual_reference_value",state.manualReference},{"manual_reference_unit","Hartree"}}},{"energy_input_mode",state.energyInputMode},{"include_absolute_table",state.includeAbsolute},{"decimal_places",state.decimalPlaces},{"include_relative_dispersion",state.includeDispersion},{"include_relative_complexation",state.includeComplexation},{"manual_energy_tables",QJsonObject{{"absolute_names",state.manualAbsoluteNames},{"absolute_values",state.manualAbsoluteValues},{"relative_names",state.manualRelativeNames},{"relative_values",state.manualRelativeValues}}},{"spin_settings",QJsonObject{{"spin_names",state.spinNames},{"spin_indices",state.spinIndices},{"show_atom_numbers",state.spinAtomNumbers}}},{"geometry_settings",QJsonObject{{"geo_names",state.geometryNames},{"geo_indices",state.geometryIndices},{"show_atom_numbers",state.geometryAtomNumbers}}},{"figure_captions",state.captions},{"captions",state.captions},{"mechanism_name",state.mechanismName},{"document_title",state.documentTitle},{"manuscript_metadata",QJsonObject{{"title",state.manuscriptTitle},{"authors",state.authors},{"affiliations",state.affiliations},{"corresponding_email",state.correspondingEmail}}},{"computational_details",state.computationalDetails},{"computational_details_edited",state.computationalDetailsEdited},{"references_text",state.referencesText},{"output_paths",QJsonObject{{"base_dir",state.baseDirectory},{"si_path",state.siPath},{"xyz_path",state.xyzPath}}},{"gaussian_snapshots",state.gaussianSnapshots},{"paired_snapshot_paths",state.pairedSnapshotPaths},{"manual_paired_paths",state.manualPairedPaths},{"generation_history",QJsonArray{}}};
  const QDir folder(QFileInfo(path).absolutePath());
  for(const char* key:{"structures","reaction_stages","molecular_figures","reference_settings","gaussian_snapshots","paired_snapshot_paths","manual_paired_paths"})
    object.insert(QLatin1String(key),portablePaths(object.value(QLatin1String(key)),folder,false));
  auto outputs=object.value("output_paths").toObject();
  for(const char* key:{"base_dir","si_path","xyz_path"}){
    const QString original=outputs.value(QLatin1String(key)).toString();
    outputs.insert(QLatin1String(key),portablePaths(original,folder,false).toString());
  }
  object.insert("output_paths",outputs);
  QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
  const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
  if(file.write(bytes)!=bytes.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;
}

bool ProjectStore::load(const QString& path, ProjectState* state, QString* error) {
  QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;}const auto document=QJsonDocument::fromJson(file.readAll());if(!document.isObject()){if(error)*error="Project must be a JSON object.";return false;}
  auto object=document.object();
  if(object.value("version").toInt(3)>=9){
    const QDir folder(QFileInfo(path).absolutePath());
    for(const char* key:{"structures","reaction_stages","molecular_figures","reference_settings","gaussian_snapshots","paired_snapshot_paths"})
      object.insert(QLatin1String(key),portablePaths(object.value(QLatin1String(key)),folder,true));
    auto outputs=object.value("output_paths").toObject();
    for(const char* key:{"base_dir","si_path","xyz_path"}){
      const QString original=outputs.value(QLatin1String(key)).toString();
      if(!original.isEmpty()&&!QFileInfo(original).isAbsolute())
        outputs.insert(QLatin1String(key),QDir::cleanPath(folder.absoluteFilePath(original)));
    }
    object.insert("output_paths",outputs);
  }
  ProjectState loaded;loaded.projectName=object.value("project_name").toString();QStringList names;for(const auto& value:object.value("structures").toArray()){const auto row=value.toObject();const auto item=row.value("main_file").toString(row.value("gaussian_file_path").toString());if(!item.isEmpty()){loaded.files.append(item);names.append(row.value("structure_name").toString(QFileInfo(item).completeBaseName()));}}loaded.structureNames=names.join('\n');
  const auto reference=object.value("reference_settings").toObject();loaded.referenceMode=reference.value("mode").toString("single");loaded.referencePath=reference.value("single_reference_path").toString(reference.value("reference_path").toString());loaded.manualReference=reference.value("manual_reference_value").toVariant().toString();loaded.energyBasis=reference.value("energy_basis").toVariant().toString();if(loaded.energyBasis.isEmpty())loaded.energyBasis="0";
  if(object.value("version").toInt(3)<=3 && (loaded.energyBasis=="0"||loaded.energyBasis=="DefaultComposite"))loaded.energyBasis="5";loaded.energyInputMode=object.value("energy_input_mode").toString("automatic");loaded.includeAbsolute=object.value("include_absolute_table").toBool(false);loaded.decimalPlaces=object.value("decimal_places").toInt(0);loaded.includeDispersion=object.value("include_relative_dispersion").toBool(false);loaded.includeComplexation=object.value("include_relative_complexation").toBool(false);
  const auto manual=object.value("manual_energy_tables").toObject();loaded.manualAbsoluteNames=manual.value("absolute_names").toString();loaded.manualAbsoluteValues=manual.value("absolute_values").toString();loaded.manualRelativeNames=manual.value("relative_names").toString();loaded.manualRelativeValues=manual.value("relative_values").toString();const auto spin=object.value("spin_settings").toObject();loaded.spinNames=spin.value("spin_names").toString();loaded.spinIndices=spin.value("spin_indices").toString();loaded.spinAtomNumbers=spin.value("show_atom_numbers").toBool(true);const auto geometry=object.value("geometry_settings").toObject();loaded.geometryNames=geometry.value("geo_names").toString();loaded.geometryIndices=geometry.value("geo_indices").toString();loaded.geometryAtomNumbers=geometry.value("show_atom_numbers").toBool(true);loaded.captions=object.value("figure_captions").toString(object.value("captions").toString());loaded.mechanismName=object.value("mechanism_name").toString();loaded.documentTitle=object.value("document_title").toString("Supporting Information");
  loaded.reactionStages=object.value("reaction_stages").toArray();loaded.figureSettings=object.value("molecular_figures").toObject();
  const auto metadata=object.value("manuscript_metadata").toObject();loaded.manuscriptTitle=metadata.value("title").toString();loaded.authors=metadata.value("authors").toString();loaded.affiliations=metadata.value("affiliations").toString();loaded.correspondingEmail=metadata.value("corresponding_email").toString();
  loaded.computationalDetails=object.value("computational_details").toString();loaded.computationalDetailsEdited=object.value("computational_details_edited").toBool(!loaded.computationalDetails.isEmpty());loaded.referencesText=object.value("references_text").toString();const auto outputs=object.value("output_paths").toObject();loaded.baseDirectory=outputs.value("base_dir").toString();loaded.siPath=outputs.value("si_path").toString();loaded.xyzPath=outputs.value("xyz_path").toString();loaded.gaussianSnapshots=object.value("gaussian_snapshots").toObject();loaded.pairedSnapshotPaths=object.value("paired_snapshot_paths").toObject();loaded.manualPairedPaths=object.value("manual_paired_paths").toObject();*state=std::move(loaded);return true;
}
