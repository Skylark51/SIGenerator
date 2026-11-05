#include "MainWindow.hpp"
#include "EditorText.hpp"
#include "si/StructureLabels.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
QVector<QVector<int>> indexGroups(const QString& text){QVector<QVector<int>> groups;for(const auto& raw:text.split(QRegularExpression(QStringLiteral("[\\r\\n|]+")),Qt::SkipEmptyParts)){QVector<int> group;for(const auto& token:raw.split(QRegularExpression(QStringLiteral("[^0-9]+")),Qt::SkipEmptyParts))group.append(token.toInt());if(!group.isEmpty())groups.append(group);}return groups;}
QVector<QStringList> valueRows(const QString& text){QVector<QStringList> rows;for(const auto& line:lines(text)){auto values=line.trimmed().split(QRegularExpression(QStringLiteral("[\\t ]+")),Qt::SkipEmptyParts);rows.append(values);}return rows;}
QString number(const std::optional<double>& value,int digits,int precision){return si::formatNumber(value,digits,precision);}
QString manualNumber(const QString& value,int){bool ok=false;const double parsed=value.toDouble(&ok);return ok?si::formatNumber(parsed,2,0):value;}
void filterRelativeTable(si::DocumentTable& table,bool dispersion,bool complexation){const auto columns=si::relativeEnergyColumns(dispersion,complexation);QStringList headers;QVector<QStringList> rows;for(int logical:columns)headers.append(table.headers[logical+1]);for(const auto& source:table.rows){QStringList row{source.value(0)};for(int logical:columns)row.append(source.value(logical+1));rows.append(row);}table.headers=QStringList{QString()}+headers;table.rows=rows;table.boldColumns.clear();for(int i=0;i<table.headers.size();++i)if(table.headers[i]==QStringLiteral("E_{Total}")||table.headers[i]==QStringLiteral("ΔG^{c}"))table.boldColumns.append(i);}
QString atomLabel(const si::GaussianResult& result,int atomId,bool showNumber=true){if(atomId>0&&static_cast<size_t>(atomId)<=result.finalGeometry.size())return QString::fromStdString(result.finalGeometry[static_cast<size_t>(atomId-1)].symbol)+(showNumber?QStringLiteral("(%1)").arg(atomId):QString());return showNumber?QStringLiteral("At(%1)").arg(atomId):QStringLiteral("At");}
}

QStringList MainWindow::preflightErrors() const{
  QStringList errors;
  const auto currentPaths=sourcePaths();
  for(const auto& value:reactionStages_){const auto stage=value.toObject();for(const auto& file:stage["files"].toArray())if(!currentPaths.contains(file.toString()))errors.append(tr("반응 단계 '%1'의 구조가 목록에서 제거되었습니다. 하위 프로젝트 · 반응 단계에서 다시 배정하세요: %2").arg(stage["name"].toString(),QFileInfo(file.toString()).fileName()));}
  if(parsing_)errors.append("Gaussian parsing is still in progress.");
  for(int row=0;row<files_->rowCount();++row)if(const auto* status=files_->item(row,6))
    for(const auto& message:status->data(Qt::UserRole).toStringList())errors.append(files_->item(row,0)->text()+": "+message);
  if(energyInputMode_->currentData().toString()=="automatic"&&referenceMode_->currentData().toString()=="manual_numeric"){
    bool ok=false;const double value=manualReference_->text().toDouble(&ok);
    if(!ok||!std::isfinite(value))errors.append("Manual reference must be a finite energy in Hartree.");
  }
  if(energyInputMode_->currentData().toString()=="manual"){
    const auto names=lines(manualRelativeNames_->toPlainText());
    const auto rows=valueRows(manualRelativeValues_->toPlainText());
    const int expected=static_cast<int>(si::relativeEnergyColumns(includeDispersion_->isChecked(),includeComplexation_->isChecked()).size());
    if(names.size()!=rows.size())errors.append(tr("상대 에너지 구조명과 값의 행 수가 일치해야 합니다."));
    for(int i=0;i<rows.size();++i){
      if(rows[i].size()!=expected&&rows[i].size()!=9)
        errors.append(tr("상대 에너지 %1행: 선택한 열의 값 %2개 또는 기존 전체 열의 값 9개를 입력하세요.").arg(i+1).arg(expected));
      for(const auto& value:rows[i]){
        bool ok=false;const double parsed=value.toDouble(&ok);
        if(value!="N/A"&&(!ok||!std::isfinite(parsed))){
          errors.append(tr("상대 에너지 %1행: 숫자 또는 N/A를 입력하세요.").arg(i+1));break;
        }
      }
    }
  }
  for(int index=0;index<reactionStages_.size();++index){
    const auto stage=reactionStages_[index].toObject();
    if(!stage.value("relative").toBool(true))continue;
    if(stage.value("structure_relative_enabled").toBool(false)){
      QStringList headers{QString(),"E_{Total}","ΔZ_{0}","ΔΔE_{Thermal}^{b}","-TΔS^{b}"};
      if(includeDispersion_->isChecked())headers.append("Δ_{Dispersion}");
      if(includeComplexation_->isChecked())headers.append("Δ_{Complexation}");headers.append("ΔG^{c}");
      const auto overrides=stage.value("structure_relative_values").toObject();
      for(const auto& member:stage.value("files").toArray()){
        const auto path=member.toString();const auto values=overrides.value(path).toObject();
        for(auto it=values.begin();it!=values.end();++it){bool valid=false;const double value=it.value().toString().toDouble(&valid);
          if(!valid||!std::isfinite(value))errors.append(tr("반응 '%1', %2: %3에 유효한 숫자를 입력하세요.").arg(stage["name"].toString(),QFileInfo(path).fileName(),it.key()));
        }
        if(stageStructureNeedsRelative(stage,path)){
          const auto row=stageRelativeRow(stage,path,headers,QStringList{QFileInfo(path).fileName()});bool valid=false;const double value=row.last().toDouble(&valid);
          if(!valid||!std::isfinite(value))errors.append(tr("반응 '%1', %2: 원자 수가 다른 구조의 ΔG 또는 열역학적 합산 성분을 입력하세요.").arg(stage["name"].toString(),QFileInfo(path).fileName()));
        }
      }
      continue;
    }
    const bool manual=stageNeedsManualRelative(index)||stage.value("manual_relative_enabled").toBool(false);
    if(!manual)continue;
    const auto members=stage.value("files").toArray();if(members.isEmpty())continue;
    const auto values=lines(stage.value("manual_relative_values").toString());
    if(values.size()!=members.size()){
      errors.append(tr("반응 '%1': 수동 ΔG 값 %2개가 필요합니다. 현재 %3개입니다.").arg(stage.value("name").toString()).arg(members.size()).arg(values.size()));
      continue;
    }
    for(int i=0;i<values.size();++i){bool valid=false;const double number=values[i].toDouble(&valid);
      if(!valid||!std::isfinite(number))errors.append(tr("반응 '%1': 수동 ΔG %2행에 유효한 숫자를 입력하세요.").arg(stage.value("name").toString()).arg(i+1));
    }
  }
  const auto spinLabels=lines(spinNames_->toPlainText());
  if(!spinLabels.isEmpty()&&spinLabels.size()!=files_->rowCount())errors.append(tr("스핀 구조명은 현재 구조 %1개와 같은 수로 입력하거나 비워 두세요. 현재 %2개입니다.").arg(files_->rowCount()).arg(spinLabels.size()));
  const auto spinGroups=indexGroups(spinIndices_->toPlainText());
  if(spinGroups.size()>1&&spinGroups.size()!=files_->rowCount())errors.append(tr("스핀 원자 ID는 공통 1행 또는 구조 수(%1개)와 같은 행 수로 입력하세요. 현재 %2행입니다.").arg(files_->rowCount()).arg(spinGroups.size()));
  for(int row=0;row<spinGroups.size();++row){
    QSet<int> seen;
    for(const int id:spinGroups[row]){
      if(id<=0||seen.contains(id)){errors.append(tr("스핀 원자 ID %1행: 양의 원자 번호를 중복 없이 입력하세요.").arg(row+1));break;}
      seen.insert(id);
    }
    if(spinGroups[row].size()!=spinGroups.front().size())errors.append(tr("스핀 원자 ID: 모든 행의 원자 수가 같아야 합니다."));
  }
  for(int recordIndex=0;recordIndex<files_->rowCount()&&!spinGroups.isEmpty();++recordIndex){
    if(spinGroups.size()>1&&spinGroups.size()!=files_->rowCount())break;
    const auto path=files_->item(recordIndex,0)->data(Qt::UserRole).toString();
    const auto parsed=parsed_.value(path);
    if(!parsed)continue;
    const auto& selected=spinGroups.size()==1?spinGroups.front():spinGroups[recordIndex];
    for(const int atomId:selected)if(atomId<=0||static_cast<size_t>(atomId)>parsed->finalGeometry.size()){
      errors.append(tr("구조 '%1': 존재하지 않는 스핀 원자 ID %2").arg(files_->item(recordIndex,0)->text()).arg(atomId));
    }
  }
  return errors;
}
bool MainWindow::validateExport(){
  refreshDocumentModel();const auto errors=preflightErrors();
  if(errors.isEmpty())return true;
  QMessageBox::warning(this,tr("Preflight Error"),errors.join('\n'));return false;
}

si::SIDocumentModel MainWindow::buildDocumentModel(){
  si::SIDocumentModel model;model.title=documentTitle_->text().trimmed().isEmpty()?QStringLiteral("Supporting Information"):documentTitle_->text().trimmed();model.mechanismName=mechanismName_->text().trimmed();model.figureCaptions=lines(captions_->toPlainText());model.precisionOverride=0;const int precision=0;const bool absoluteRequested=includeAbsolute_->isChecked()||std::any_of(reactionStages_.begin(),reactionStages_.end(),[](const auto& value){return value.toObject().value("absolute").toBool(false);});const auto labels=structureLabels();const auto paths=sourcePaths();
  model.manuscriptTitle=manuscriptTitle_->text();model.authors=authors_->text();model.affiliations=affiliations_->text();model.correspondingEmail=correspondingEmail_->text();model.referencesText=referencesText_->toPlainText();
  auto records=energyRecords();geometryAutoInfo_->clear();
  const auto detailsBasis=energyInputMode_->currentData().toString()==QStringLiteral("automatic")&&referenceMode_->currentData().toString()==QStringLiteral("single")
    ?si::EnergyBasis::LegacyComposite:static_cast<si::EnergyBasis>(energyBasis_->currentData().toInt());
  if(!detailsEdited_){QStringList warnings;const auto draft=si::computationalDetails(records,detailsBasis,&warnings);QSignalBlocker blocked(computationalDetails_);computationalDetails_->setPlainText(draft);}
  model.computationalDetails=computationalDetails_->toPlainText();
  for(const auto& record:records)model.coordinates.append({QString::fromStdString(record.name),record.frequency.finalGeometry,record.frequency.charge,record.frequency.multiplicity,record.singlePoint?record.singlePoint->scfEnergy:std::nullopt});
  QByteArray geometryBytes;QDataStream signatureStream(&geometryBytes,QIODevice::WriteOnly);
  signatureStream<<paths<<labels;
  for(const auto& coordinate:model.coordinates)for(const auto& atom:coordinate.atoms)signatureStream<<atom.atomicNumber<<atom.xyz[0]<<atom.xyz[1]<<atom.xyz[2];
  for(const auto& value:reactionStages_){const auto stage=value.toObject();signatureStream<<stage.value("id").toString(stage["name"].toString());for(const auto& path:stage["files"].toArray())signatureStream<<path.toString();}
  const auto signature=QCryptographicHash::hash(geometryBytes,QCryptographicHash::Sha256);
  if(signature!=figureSignature_){preparedFigures_.clear();preparedStageFigures_.clear();figureSignature_=signature;}
  model.figures=preparedFigures_;
  if(!reactionStages_.isEmpty()){model.figures.clear();model.figureCaptions.clear();for(const auto& value:reactionStages_){const auto stage=value.toObject();const auto id=stage.value("id").toString(stage["name"].toString());const auto captions=lines(stage["figure_captions"].toString());const auto figures=preparedStageFigures_.value(id);for(int i=0;i<figures.size();++i){auto figure=figures[i];figure.stageId=id;figure.documentNumber=model.figures.size()+1;model.figures.append(figure);model.figureCaptions.append(captions.value(i,stage["name"].toString()+" — "+figure.structureNames.join(" / ")));}}}
  if(energyInputMode_->currentData().toString()==QStringLiteral("manual")){
    if(absoluteRequested){si::DocumentTable table{si::DocumentSection::AbsoluteEnergy,tr("Absolute energy values of %1 in Kcal/Mol.").arg(si::displayMechanism(model)),{QString(),QStringLiteral("E_{Def2-SVP}"),QStringLiteral("E_{Def2-TZVPP}"),QStringLiteral("Z_{0}"),QStringLiteral("E_{Thermal}"),QStringLiteral("S"),QStringLiteral("Dispersion")},{},{}};const auto names=lines(manualAbsoluteNames_->toPlainText());const auto rows=valueRows(manualAbsoluteValues_->toPlainText());for(int i=0;i<std::max(names.size(),rows.size());++i){QStringList row{i<names.size()?names[i]:QString()};if(i<rows.size())for(const auto& value:rows[i])row.append(manualNumber(value,precision));table.rows.append(row);}if(!table.rows.isEmpty())model.tables.append(table);}
    si::DocumentTable table{si::DocumentSection::RelativeEnergy,tr("Relative energy values of %1 in Kcal/Mol.").arg(si::displayMechanism(model)),{QString(),QStringLiteral("ΔE"),QStringLiteral("ΔΔE^{a}"),QStringLiteral("E_{Total}"),QStringLiteral("ΔZ_{0}"),QStringLiteral("ΔΔE_{Thermal}^{b}"),QStringLiteral("-TΔS^{b}"),QStringLiteral("Δ_{Dispersion}"),QStringLiteral("Δ_{Complexation}"),QStringLiteral("ΔG^{c}")},{},{}};
    const auto names=lines(manualRelativeNames_->toPlainText());
    const auto rows=valueRows(manualRelativeValues_->toPlainText());
    const auto columns=si::relativeEnergyColumns(includeDispersion_->isChecked(),includeComplexation_->isChecked());
    for(int i=0;i<rows.size();++i){
      QStringList row{names.value(i)};
      for(int logical=0;logical<9;++logical)row.append(QString());
      // Full-width saved input retains its original column positions. Compact
      // input contains only the selected columns, with Gibbs always last.
      for(int j=0;j<rows[i].size();++j){
        const int logical=rows[i].size()==9?j:(j<static_cast<int>(columns.size())?columns[j]:-1);
        if(logical<0)continue;
        auto value=manualNumber(rows[i][j],precision);
        if(rows[i][j].startsWith('+')&&!value.startsWith('+')&&!value.startsWith('-'))value.prepend('+');
        row[logical+1]=value;
      }
      table.rows.append(row);
    }
    filterRelativeTable(table,includeDispersion_->isChecked(),includeComplexation_->isChecked());
    if(!table.rows.isEmpty())model.tables.append(table);
  }else if(!records.empty()){
    const auto basis=static_cast<si::EnergyBasis>(energyBasis_->currentData().toInt());for(auto& record:records)(void)si::selectEnergy(record,basis);
    if(absoluteRequested){
      si::DocumentTable table{si::DocumentSection::AbsoluteEnergy,
        tr("Absolute energy values of %1. Electronic and dispersion energies in Hartree; ZPE and thermal energy in kcal/mol; entropy in cal mol^{-1} K^{-1}.").arg(si::displayMechanism(model)),
        {QString(),"E_{Def2-SVP}","E_{Def2-TZVPP}","Z_{0}","E_{Thermal}","S","Dispersion"},{},{}};
      for(const auto& record:records){
        const auto values=si::legacyAbsoluteEnergyValues(record);
        QStringList row{QString::fromStdString(record.name)};
        for(int col=0;col<6;++col)row.append(number(values[col],2,0));
        table.rows.append(row);
      }
      model.tables.append(table);
    }
    if(referenceMode_->currentData().toString()!=QStringLiteral("manual_relative_only")){
      size_t refIndex=0;
      const auto requested=QFileInfo(reference_->currentData().toString()).absoluteFilePath();
      for(size_t i=0;i<records.size();++i)
        if(QFileInfo(QString::fromStdWString(records[i].frequency.path.wstring())).absoluteFilePath().compare(requested,Qt::CaseInsensitive)==0){refIndex=i;break;}
      const bool manualRef=referenceMode_->currentData().toString()==QStringLiteral("manual_numeric");
      si::DocumentTable table{si::DocumentSection::RelativeEnergy,
        tr("Relative energy values of %1 in kcal/mol.").arg(si::displayMechanism(model)),
        {QString(),"ΔE","ΔΔE^{a}","E_{Total}","ΔZ_{0}","ΔΔE_{Thermal}^{b}","-TΔS^{b}","Δ_{Dispersion}","Δ_{Complexation}","ΔG^{c}"},{},{}};
      std::vector<si::LegacyRelativeEnergy> relative;
      if(!manualRef)table.thermochemistryNote=si::relativeTemperatureNote();
      if(!manualRef)relative=si::legacyRelativeEnergies(records,records[refIndex],includeDispersion_->isChecked(),includeComplexation_->isChecked());
      else {
        table.thermochemistryNote=tr("Thermal components are not evaluated for a manually entered reference.");
        bool ok=false;const double ref=manualReference_->text().toDouble(&ok);
        for(const auto& record:records){
          si::LegacyRelativeEnergy entry{record.name,{}};
          if(ok&&std::isfinite(ref)&&record.selectedHartree){
            const double delta=(*record.selectedHartree-ref)*si::HartreeToKcal;
            entry.kcalTerms={delta,0.0,delta,0.0,0.0,0.0,0.0,0.0,delta};
          }
          relative.push_back(entry);
        }
      }
      for(const auto& entry:relative){
        QStringList row{QString::fromStdString(entry.name)};
        for(int col=0;col<9;++col){
          auto text=number(entry.kcalTerms[col],2,precision);
          if(entry.kcalTerms[col]){
            const bool correction=col==1||col==3||col==4||col==5;
            const bool optionalCorrection=col==6||col==7;
            if(((correction&&*entry.kcalTerms[col]>0)||(optionalCorrection&&*entry.kcalTerms[col]>=0))&&!text.startsWith('-'))text.prepend('+');
          }
          row.append(text);
        }
        table.rows.append(row);
      }
      filterRelativeTable(table,includeDispersion_->isChecked(),includeComplexation_->isChecked());model.tables.append(table);
    }
  }
  if(!records.empty()){
    const auto groups=indexGroups(spinIndices_->toPlainText());
    QVector<int> ids;
    if(groups.isEmpty()){
      std::unordered_map<int,double> maxima;
      for(const auto& record:records)for(const auto& spin:record.frequency.spinDensities)
        maxima[spin.atomIndex]=std::max(maxima[spin.atomIndex],std::abs(spin.spin));
      std::vector<std::pair<int,double>> ranked(maxima.begin(),maxima.end());
      std::sort(ranked.begin(),ranked.end(),[](const auto&a,const auto&b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
      for(size_t i=0;i<std::min<size_t>(6,ranked.size());++i)ids.append(ranked[i].first);
    }else ids=groups.front();
    const bool completeSpinMapping=groups.isEmpty()||groups.size()==1||groups.size()==static_cast<int>(records.size());
    const bool perStructureIds=groups.size()==static_cast<int>(records.size())&&groups.size()>1&&
      std::any_of(groups.cbegin()+1,groups.cend(),[&](const QVector<int>& group){return group!=groups.front();});
    if(!ids.isEmpty()&&completeSpinMapping){
      bool allTz=true;for(const auto& record:records)allTz=allTz&&record.frequency.spinBasisLevel=="TZVPP";
      QString caption=tr("Mulliken Spin Density Distributions of %1.").arg(si::displayMechanism(model));
      if(allTz)caption+=tr(" Calculated at the Def2-TZVPP level.");
      si::DocumentTable table{si::DocumentSection::SpinDensity,caption,{QString()},{},{}};
      int width=ids.size();for(const auto& group:groups)width=std::max(width,static_cast<int>(group.size()));
      for(int col=0;col<width;++col)table.headers.append(perStructureIds?tr("Selected atom %1").arg(col+1):(col<ids.size()?atomLabel(records.front().frequency,ids[col],spinAtomNumbers_->isChecked()):tr("Atom %1").arg(col+1)));
      table.headers.append(QStringLiteral("Rest"));table.headers.append(QStringLiteral("Sum"));
      const auto names=lines(spinNames_->toPlainText());
      for(size_t index=0;index<records.size();++index){
        const auto& record=records[index];
        const auto current=groups.isEmpty()?ids:groups[std::min<int>(static_cast<int>(index),groups.size()-1)];
        QSet<int> selectedIds;for(const int id:current)selectedIds.insert(id);
        double rest=0,total=0;
        for(const auto& spin:record.frequency.spinDensities){total+=spin.spin;if(!selectedIds.contains(spin.atomIndex))rest+=spin.spin;}
        QStringList row{names.size()==static_cast<int>(records.size())?names[static_cast<int>(index)]:QString::fromStdString(record.name)};
        for(const int id:current){
          std::optional<double> value;
          for(const auto& spin:record.frequency.spinDensities)if(spin.atomIndex==id){value=spin.spin;break;}
          row.append(perStructureIds?(atomLabel(record.frequency,id,true)+QStringLiteral(": ")+number(value,3,precision)):number(value,3,precision));
        }
        while(row.size()<table.headers.size()-2)row.append(QStringLiteral("N/A"));
        row.append(record.frequency.spinDensities.empty()?QStringLiteral("N/A"):si::formatNumber(rest,3,precision));
        row.append(record.frequency.spinDensities.empty()?QStringLiteral("N/A"):si::formatNumber(total,3,precision));
        table.rows.append(row);
      }
      model.tables.append(table);
    }
    auto geometryGroups=indexGroups(geometryIndices_->toPlainText());
    const bool automaticGeometry=geometryIndices_->toPlainText().trimmed().isEmpty();
    QSet<size_t> compatibleGeometryRecords;const si::GaussianResult* geometryLabels=&records.front().frequency;
    geometryAutoInfo_->clear();
    if(automaticGeometry){
      const auto suggestions=si::suggestGeometry(records);for(const auto index:suggestions.recordIndices)compatibleGeometryRecords.insert(index);if(!suggestions.recordIndices.empty())geometryLabels=&records[suggestions.recordIndices.front()].frequency;
      for(const auto& pair:suggestions.distances)geometryGroups.append(QVector<int>{pair[0],pair[1]});
      if(suggestions.compatibleStructures<2)geometryAutoInfo_->setText(tr("자동 분석: 원자 수와 원소 순서가 같은 구조가 2개 이상 필요합니다. 원자 번호도 서로 대응해야 합니다."));
      else if(geometryGroups.isEmpty())geometryAutoInfo_->setText(tr("자동 분석: H–H를 제외한 결합 거리 변화 후보가 없습니다. 필요하면 원자 ID를 직접 입력하세요."));
      else {
        const double maximum=*std::max_element(suggestions.rmsd.begin(),suggestions.rmsd.end());
        geometryAutoInfo_->setText(tr("RMSD 자동 분석: %1개 구조, 최대 RMSD %2 Å, 거리 %3개. 원자 번호는 구조 간에 대응해야 합니다.").arg(suggestions.compatibleStructures).arg(maximum,0,'f',3).arg(geometryGroups.size()));
      }
    }
    if(!geometryGroups.isEmpty()){si::DocumentTable table{si::DocumentSection::SelectedGeometry,tr("Selected Geometries of %1 in Å and °.").arg(si::displayMechanism(model)),{QString()},{},{}};for(const auto& group:geometryGroups){QStringList atoms;for(const int id:group)atoms.append(atomLabel(*geometryLabels,id,geometryAtomNumbers_->isChecked()));table.headers.append(atoms.join('-')+QStringLiteral(" (%1)").arg(group.size()==2?QStringLiteral("Å"):QStringLiteral("°")));}const auto names=lines(geometryNames_->toPlainText());for(size_t index=0;index<records.size();++index){const auto& record=records[index];QStringList row{names.size()==static_cast<int>(records.size())?names[static_cast<int>(index)]:QString::fromStdString(record.name)};for(const auto& group:geometryGroups){if(automaticGeometry&&!compatibleGeometryRecords.contains(index)){row.append(QStringLiteral("N/A"));continue;}std::optional<double> value;if(group.size()==2)value=si::distance(record.frequency.finalGeometry,group[0],group[1]);else if(group.size()==3)value=si::angle(record.frequency.finalGeometry,group[0],group[1],group[2]);else if(group.size()==4)value=si::dihedral(record.frequency.finalGeometry,group[0],group[1],group[2],group[3]);row.append(number(value,3,precision));}table.rows.append(row);}model.tables.append(table);}
    QVector<QStringList> excited;QVector<QStringList> irc;for(const auto& record:records){for(const auto& state:record.frequency.excitedStates)excited.append({QString::fromStdString(record.name),QString::number(state.index),si::formatNumber(state.energyEv,4,precision),number(state.wavelengthNm,1,precision),number(state.oscillatorStrength,4,precision)});for(const auto& point:record.frequency.ircPoints)irc.append({QString::fromStdString(record.name),QString::number(point.index),QString::fromStdString(point.direction),number(point.scfEnergy,8,precision)});}if(!excited.isEmpty())model.tables.append({si::DocumentSection::Other,tr("TD-DFT excited states"),{tr("Structure"),tr("State"),tr("Energy (eV)"),tr("Wavelength (nm)"),tr("f")},excited,{}});if(!irc.isEmpty())model.tables.append({si::DocumentSection::Other,tr("IRC reaction-coordinate points"),{tr("Structure"),tr("Point"),tr("Direction"),tr("SCF energy (Hartree)")},irc,{}});
  }
  applyReactionStages(model);for(int i=0;i<model.tables.size();++i)model.tables[i].documentNumber=i+1;return model;
}

void MainWindow::scheduleDocumentRefresh(){if(previewTimer_)previewTimer_->start();}
void MainWindow::refreshDocumentModel(){if(parsing_)return;refreshPreflight();refreshStageNavigation();documentModel_=buildDocumentModel();refreshStageWorkspace();refreshScopedPreview();}
void MainWindow::updateGeometryFeedback(const QVector<int>& ids){if(ids.isEmpty()){interactionInfo_->setText(tr("원자 2/3/4개를 순서대로 선택하세요. (Select 2/3/4 atoms.)"));return;}const int row=files_->currentRow();if(row<0)return;const auto result=parsed_.value(files_->item(row,0)->data(Qt::UserRole).toString());if(!result)return;QStringList labels;for(const int id:ids)labels.append(atomLabel(*result,id));QString value;if(ids.size()==2){const auto calculated=si::distance(result->finalGeometry,ids[0],ids[1]);if(calculated)value=QStringLiteral(" = %1 Å").arg(*calculated,0,'f',3);}else if(ids.size()==3){const auto calculated=si::angle(result->finalGeometry,ids[0],ids[1],ids[2]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}else if(ids.size()==4){const auto calculated=si::dihedral(result->finalGeometry,ids[0],ids[1],ids[2],ids[3]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}interactionInfo_->setText(labels.join(QStringLiteral(" – "))+value);}

void MainWindow::applyReactionStages(si::SIDocumentModel& model){
  if(reactionStages_.isEmpty())return;
  auto stages=reactionStages_;const auto paths=sourcePaths();QSet<QString> assigned;
  for(const auto& value:stages)for(const auto& path:value.toObject()["files"].toArray())assigned.insert(path.toString());
  QJsonArray remaining;for(const auto& path:paths)if(!assigned.contains(path))remaining.append(path);
  if(!remaining.isEmpty())stages.append(QJsonObject{{"name",tr("미배정 구조")},{"files",remaining},{"reference",remaining.first()}});
  const auto records=energyRecords();QVector<si::DocumentTable> tables;const auto labels=structureLabels();
  auto manualEnergy=[&](const si::DocumentTable& table){return energyInputMode_->currentData().toString()=="manual"&&(table.section==si::DocumentSection::AbsoluteEnergy||table.section==si::DocumentSection::RelativeEnergy);};
  auto canSplit=[&](const si::DocumentTable& table){
    if(table.section==si::DocumentSection::Other||table.section==si::DocumentSection::Validation)return false;
    if(manualEnergy(table)){QSet<QString> seen;for(const auto& row:table.rows){if(!labels.contains(row.value(0))||labels.count(row.value(0))!=1||seen.contains(row.value(0)))return false;seen.insert(row.value(0));}return !table.rows.isEmpty();}
    return table.rows.size()==paths.size();
  };
  for(const auto& value:stages){const auto stage=value.toObject();const auto id=stage.value("id").toString(stage["name"].toString());model.stageOrder.append(id);model.stageNames.insert(id,stage["name"].toString());
  int sourceStageIndex=-1;for(int index=0;index<reactionStages_.size();++index)if(reactionStages_[index].toObject().value("id").toString(reactionStages_[index].toObject().value("name").toString())==id){sourceStageIndex=index;break;}
  for(const auto& table:model.tables){
    const QString key=table.section==si::DocumentSection::AbsoluteEnergy?"absolute":table.section==si::DocumentSection::RelativeEnergy?"relative":table.section==si::DocumentSection::SpinDensity?"spin":table.section==si::DocumentSection::SelectedGeometry?"geometry":"";
    if(key=="geometry"&&geometryIndices_->toPlainText().trimmed().isEmpty())continue; // Recalculate automatically for each reaction stage.
    if(key.isEmpty()||!canSplit(table))continue;
    {if(!stage.value(key).toBool(key=="absolute"?includeAbsolute_->isChecked():true))continue;const auto members=stage["files"].toArray();auto split=table;split.stageId=stage.value("id").toString(stage["name"].toString());split.rows.clear();QStringList splitPaths;std::vector<si::EnergyRecord> selected;
      for(const auto& member:members){const int i=paths.indexOf(member.toString());if(i<0)continue;if(manualEnergy(table)){for(const auto& row:table.rows)if(row.value(0)==labels[i]){split.rows.append(row);splitPaths.append(member.toString());}}else {split.rows.append(table.rows[i]);splitPaths.append(member.toString());}if(i<static_cast<int>(records.size()))selected.push_back(records[i]);}
      if(split.rows.isEmpty())continue;
      if(key!="geometry")split.caption=stage["name"].toString()+": "+table.caption;
      if(key=="spin"&&spinIndices_->toPlainText().trimmed().isEmpty()&&!selected.empty()){
        // The global six-atom ranking can select IDs absent from another
        // reaction. Rank only atoms reported for every structure in this stage.
        std::unordered_map<int,double> maxima;
        for(const auto& record:selected)for(const auto& spin:record.frequency.spinDensities)
          maxima[spin.atomIndex]=std::max(maxima[spin.atomIndex],std::abs(spin.spin));
        std::vector<std::pair<int,double>> ranked(maxima.begin(),maxima.end());
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
        QVector<int> commonIds;
        for(const auto& candidate:ranked){
          std::string symbol;bool common=true;
          for(const auto& record:selected){
            const auto& spins=record.frequency.spinDensities;
            const auto found=std::find_if(spins.begin(),spins.end(),[&](const auto& spin){return spin.atomIndex==candidate.first;});
            if(found==spins.end()||(!symbol.empty()&&symbol!=found->atom)){common=false;break;}
            symbol=found->atom;
          }
          if(common)commonIds.append(candidate.first);
          if(commonIds.size()==6)break;
        }
        if(commonIds.isEmpty())continue; // No common Mulliken atom data can be compared.
        const auto sourceRows=split.rows;
        split.headers={QString()};
        for(const int atomId:commonIds)split.headers.append(atomLabel(selected.front().frequency,atomId,spinAtomNumbers_->isChecked()));
        split.headers.append(QStringLiteral("Rest"));split.headers.append(QStringLiteral("Sum"));
        split.rows.clear();
        for(size_t rowIndex=0;rowIndex<selected.size();++rowIndex){
          const auto& record=selected[rowIndex];double total=0.0,rest=0.0;
          QSet<int> chosen;for(const int id:commonIds)chosen.insert(id);
          for(const auto& spin:record.frequency.spinDensities){
            total+=spin.spin;if(!chosen.contains(spin.atomIndex))rest+=spin.spin;
          }
          QStringList row{sourceRows.value(static_cast<int>(rowIndex)).value(0,QString::fromStdString(record.name))};
          for(const int atomId:commonIds){
            const auto& spins=record.frequency.spinDensities;
            const auto found=std::find_if(spins.begin(),spins.end(),[&](const auto& spin){return spin.atomIndex==atomId;});
            row.append(si::formatNumber(found->spin,3,0));
          }
          row.append(si::formatNumber(rest,3,0));row.append(si::formatNumber(total,3,0));
          split.rows.append(row);
        }
      }
      if(key=="relative"&&!stage.value("structure_relative_enabled").toBool(false)&&(stage.value("manual_relative_enabled").toBool(false)||(sourceStageIndex>=0&&stageNeedsManualRelative(sourceStageIndex)))){
        const auto values=lines(stage.value("manual_relative_values").toString());
        split.headers={tr("Structure"),tr("ΔG (kcal/mol)")};
        split.caption=stage["name"].toString()+tr(": Manually assigned relative Gibbs energies (kcal/mol).");
        split.rows.clear();
        for(int memberIndex=0;memberIndex<members.size();++memberIndex){
          const int sourceIndex=paths.indexOf(members[memberIndex].toString());if(sourceIndex<0)continue;
          bool valid=false;const double value=values.value(memberIndex).toDouble(&valid);
          split.rows.append({labels[sourceIndex],valid&&std::isfinite(value)?number(value,2,model.precisionOverride):tr("입력 필요")});
        }
        tables.append(split);continue;
      }
      if(key=="relative"&&energyInputMode_->currentData().toString()=="automatic"&&referenceMode_->currentData().toString()=="single"&&!selected.empty()){
        size_t ref=0;for(size_t i=0;i<selected.size();++i)if(QFileInfo(QString::fromStdWString(selected[i].frequency.path.wstring())).absoluteFilePath().compare(QFileInfo(stage["reference"].toString()).absoluteFilePath(),Qt::CaseInsensitive)==0)ref=i;
        const auto relative=si::legacyRelativeEnergies(selected,selected[ref],includeDispersion_->isChecked(),includeComplexation_->isChecked());
        split.rows.clear();const auto columns=si::relativeEnergyColumns(includeDispersion_->isChecked(),includeComplexation_->isChecked());
        for(const auto& entry:relative){QStringList row{QString::fromStdString(entry.name)};for(int col:columns){auto text=number(entry.kcalTerms[col],2,model.precisionOverride);if(entry.kcalTerms[col]&&(((col==1||col==3||col==4||col==5)&&*entry.kcalTerms[col]>0)||((col==6||col==7)&&*entry.kcalTerms[col]>=0))&&!text.startsWith('-'))text.prepend('+');row.append(text);}split.rows.append(row);}
      }
      if(key=="relative"&&stage.value("structure_relative_enabled").toBool(false)){
        split.hasManualRelativeValues=!stage.value("structure_relative_values").toObject().isEmpty();
        if(split.hasManualRelativeValues)split.caption+=tr(" Selected energy and thermodynamic values were manually assigned.");
        for(int row=0;row<split.rows.size()&&row<splitPaths.size();++row)
          split.rows[row]=stageRelativeRow(stage,splitPaths[row],split.headers,split.rows[row]);
      }
      tables.append(split);
    }
  }
  if(geometryIndices_->toPlainText().trimmed().isEmpty()){
    std::vector<si::EnergyRecord> geometryRecords;
    QVector<int> sourceIndices;
    for(const auto& member:stage["files"].toArray()){
      const int index=paths.indexOf(member.toString());
      if(index>=0&&index<static_cast<int>(records.size())){
        geometryRecords.push_back(records[static_cast<size_t>(index)]);
        sourceIndices.append(index);
      }
    }
    const auto suggestion=si::suggestGeometry(geometryRecords);
    if(!suggestion.distances.empty()){
      si::DocumentTable geometryTable{si::DocumentSection::SelectedGeometry,
        tr("Selected Geometries of %1 in Å and °.").arg(si::displayMechanism(model)),
        {QString()},{},{}};
      geometryTable.stageId=id;
      const auto& labelSource=geometryRecords[suggestion.recordIndices.front()].frequency;
      for(const auto& pair:suggestion.distances)
        geometryTable.headers.append(atomLabel(labelSource,pair[0],geometryAtomNumbers_->isChecked())+"-"+atomLabel(labelSource,pair[1],geometryAtomNumbers_->isChecked())+QStringLiteral(" (Å)"));
      QSet<size_t> compatible;
      for(const auto index:suggestion.recordIndices)compatible.insert(index);
      const auto manuallyNamed=lines(geometryNames_->toPlainText());
      for(size_t index=0;index<geometryRecords.size();++index){
        const auto& record=geometryRecords[index];
        const int sourceIndex=sourceIndices[static_cast<int>(index)];
        QStringList row{manuallyNamed.size()==paths.size()?manuallyNamed[sourceIndex]:QString::fromStdString(record.name)};
        for(const auto& pair:suggestion.distances)
          row.append(compatible.contains(index)?number(si::distance(record.frequency.finalGeometry,pair[0],pair[1]),3,0):QStringLiteral("N/A"));
        geometryTable.rows.append(row);
      }
      tables.append(geometryTable);
    }
  }
  }
  for(const auto& table:model.tables)if(!canSplit(table))tables.append(table);
  model.tables=tables;
}
