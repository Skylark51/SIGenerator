#include <QPushButton>
#include "MainWindow.hpp"
#include "EditorText.hpp"
#include "GaussianSnapshot.hpp"
#include "ProjectManagerDialog.hpp"
#include "ProjectStore.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QTextEdit>
#include <algorithm>

void MainWindow::saveProject(){
  if(!currentProjectPath_.isEmpty()){saveProjectToPath(currentProjectPath_);return;}
  const auto path=QFileDialog::getSaveFileName(this,tr("SI 프로젝트 저장"),baseDirectory_->text()+"/SI_Project.si_project.json",tr("SI Project (*.si_project.json)"));
  if(!path.isEmpty())saveProjectToPath(path);
}
bool MainWindow::saveActiveProject(){return currentProjectPath_.isEmpty()||saveProjectToPath(currentProjectPath_);}
void MainWindow::closeEvent(QCloseEvent* event){if(!saveActiveProject()){event->ignore();return;}event->accept();emit editorClosed();}
bool MainWindow::saveProjectToPath(const QString& path){
  if(parsing_){QMessageBox::warning(this,tr("프로젝트 저장 대기"),tr("Gaussian 파일 해석이 끝난 뒤 저장해야 추출 데이터가 모두 포함됩니다."));return false;}
  for(const auto& source:sourcePaths())if(!parsed_.contains(source)){QMessageBox::warning(this,tr("프로젝트 저장 실패"),tr("추출 결과가 없는 구조가 있습니다: %1").arg(source));return false;}
  refreshDocumentModel();
  const auto state=captureProjectState();
  QString error;if(!ProjectStore::save(path,state,&error)){QMessageBox::critical(this,tr("저장 실패 (Save Failed)"),error);return false;}currentProjectPath_=path;ProjectManagerDialog::rememberProject(path);status_->setText(tr("프로젝트 저장됨: %1").arg(path));return true;}
ProjectState MainWindow::captureProjectState() const{
  ProjectState state;
  state.projectName=projectName_;
  state.files=sourcePaths();
  state.structureNames=structureLabels().join('\n');
  state.referenceMode=referenceMode_->currentData().toString();
  state.referencePath=reference_->currentData().toString();
  state.manualReference=manualReference_->text();
  state.energyInputMode=energyInputMode_->currentData().toString();
  state.energyBasis=QString::number(energyBasis_->currentData().toInt());
  state.manualAbsoluteNames=manualAbsoluteNames_->toPlainText();
  state.manualAbsoluteValues=manualAbsoluteValues_->toPlainText();
  state.manualRelativeNames=manualRelativeNames_->toPlainText();
  state.manualRelativeValues=manualRelativeValues_->toPlainText();
  state.spinNames=spinNames_->toPlainText();
  state.spinIndices=spinIndices_->toPlainText();
  state.spinAtomNumbers=spinAtomNumbers_->isChecked();
  state.geometryNames=geometryNames_->toPlainText();
  state.geometryIndices=geometryIndices_->toPlainText();
  state.geometryAtomNumbers=geometryAtomNumbers_->isChecked();
  state.captions=captions_->toPlainText();
  state.mechanismName=mechanismName_->text();
  state.documentTitle=documentTitle_->text();
  state.manuscriptTitle=manuscriptTitle_->text();
  state.authors=authors_->text();
  state.affiliations=affiliations_->text();
  state.correspondingEmail=correspondingEmail_->text();
  state.computationalDetails=computationalDetails_->toPlainText();
  state.computationalDetailsEdited=detailsEdited_;
  state.referencesText=referencesText_->toPlainText();
  state.figureSettings=figureSettings_;
  state.reactionStages=reactionStages_;
  state.includeAbsolute=includeAbsolute_->isChecked();
  state.decimalPlaces=0;
  state.includeDispersion=includeDispersion_->isChecked();
  state.includeComplexation=includeComplexation_->isChecked();
  state.baseDirectory=baseDirectory_->text();
  state.siPath=siOutputPath_->text();
  state.xyzPath=xyzOutputPath_->text();
  for(const auto& source:state.files){
    if(const auto result=parsed_.value(source))
      state.gaussianSnapshots.insert(source,si::GaussianSnapshot::encode(*result));
    const auto pairedPath=pairedPaths_.value(source);
    if(!pairedPath.isEmpty()){
      if(const auto paired=parsed_.value(pairedPath);paired&&useSinglePoint(source,*parsed_.value(source),*paired)){
        state.pairedSnapshotPaths.insert(source,pairedPath);
        state.gaussianSnapshots.insert(pairedPath,si::GaussianSnapshot::encode(*paired));
      }
    }
  }
  for(auto it=manualPairedPaths_.cbegin();it!=manualPairedPaths_.cend();++it)state.manualPairedPaths.insert(it.key(),it.value());
  return state;
}
void MainWindow::openProject(){
  const auto path=QFileDialog::getOpenFileName(this,tr("SI 프로젝트 열기 (Open Project)"),baseDirectory_->text(),tr("SI Project (*.si_project.json *.json)"));if(path.isEmpty())return;
  loadProjectPath(path);
}
bool MainWindow::loadProjectPath(const QString& path){
  ProjectState state;QString error;if(!ProjectStore::load(path,&state,&error)){QMessageBox::critical(this,tr("열기 실패 (Open Failed)"),error);return false;}
  if(!currentProjectPath_.isEmpty()&&!saveActiveProject())return false;
  applyProjectState(path,state);
  return true;
}
void MainWindow::applyProjectState(const QString& path,const ProjectState& state){
  previousDetails_.clear();restoreDetails_->setEnabled(false);
  pendingStructureLabels_=lines(state.structureNames);pendingLabelsByPath_.clear();for(int i=0;i<state.files.size();++i)pendingLabelsByPath_.insert(QFileInfo(state.files[i]).absoluteFilePath(),pendingStructureLabels_.value(i,QFileInfo(state.files[i]).completeBaseName()));
  referenceMode_->setCurrentIndex(std::max(0,referenceMode_->findData(state.referenceMode)));pendingReferencePath_=state.referencePath;manualReference_->setText(state.manualReference);energyInputMode_->setCurrentIndex(std::max(0,energyInputMode_->findData(state.energyInputMode)));bool ok=false;const int basis=state.energyBasis.toInt(&ok);if(ok&&basis==static_cast<int>(si::EnergyBasis::LegacyComposite)&&energyBasis_->findData(basis)<0)energyBasis_->addItem("Legacy composite (saved project)",basis);if(ok)energyBasis_->setCurrentIndex(std::max(0,energyBasis_->findData(basis)));manualAbsoluteNames_->setPlainText(state.manualAbsoluteNames);manualAbsoluteValues_->setPlainText(state.manualAbsoluteValues);manualRelativeNames_->setPlainText(state.manualRelativeNames);manualRelativeValues_->setPlainText(state.manualRelativeValues);spinNames_->setPlainText(state.spinNames);spinIndices_->setPlainText(state.spinIndices);spinAtomNumbers_->setChecked(state.spinAtomNumbers);geometryNames_->setPlainText(state.geometryNames);geometryIndices_->setPlainText(state.geometryIndices);geometryAtomNumbers_->setChecked(state.geometryAtomNumbers);captions_->setPlainText(state.captions);mechanismName_->setText(state.mechanismName);documentTitle_->setText(state.documentTitle);manuscriptTitle_->setText(state.manuscriptTitle);authors_->setText(state.authors);affiliations_->setText(state.affiliations);correspondingEmail_->setText(state.correspondingEmail);computationalDetails_->setPlainText(state.computationalDetails);detailsEdited_=state.computationalDetailsEdited;referencesText_->setPlainText(state.referencesText);figureSettings_=state.figureSettings;reactionStages_=state.reactionStages;
  manualPairedPaths_.clear();
  for(auto it=state.manualPairedPaths.constBegin();it!=state.manualPairedPaths.constEnd();++it)manualPairedPaths_.insert(it.key(),it.value().toString());preparedFigures_.clear();preparedStageFigures_.clear();includeAbsolute_->setChecked(state.includeAbsolute);decimalPlaces_->setValue(0);includeDispersion_->setChecked(state.includeDispersion);includeComplexation_->setChecked(state.includeComplexation);const QDir projectDirectory(QFileInfo(path).absolutePath());
  baseDirectory_->setText(QDir(state.baseDirectory).exists()?state.baseDirectory:projectDirectory.absolutePath());
  auto relocatedOutput=[&](const QString& previous,const QString& fallback){
    if(previous.trimmed().isEmpty())return projectDirectory.filePath(fallback);
    const QFileInfo saved(previous);
    if(saved.isRelative()||!saved.dir().exists())return projectDirectory.filePath(saved.fileName());
    return previous;
  };
  siOutputPath_->setText(relocatedOutput(state.siPath,QStringLiteral("SI.docx")));
  xyzOutputPath_->setText(relocatedOutput(state.xyzPath,QStringLiteral("structures.xyz")));
  fileNames_->clear();fileNamesInfo_->clear();currentProjectPath_=QFileInfo(path).absoluteFilePath();projectName_=state.projectName.isEmpty()?QFileInfo(path).completeBaseName().remove(".si_project"):state.projectName;
  setWindowTitle(tr("%1 — SI Generator").arg(projectName_));ProjectManagerDialog::rememberProject(currentProjectPath_);
  auto paired=state.pairedSnapshotPaths;
  for(auto it=state.manualPairedPaths.constBegin();it!=state.manualPairedPaths.constEnd();++it)paired.insert(it.key(),it.value());
  activeStage_=reactionStages_.isEmpty()?-2:0;parsePaths(state.files,false,false,QStringLiteral("@project-restore"),state.gaussianSnapshots,paired);refreshStageNavigation();refreshStageWorkspace();scrollTo(filesSection_);status_->setText(tr("프로젝트: %1 · 전환 및 종료 시 자동 저장").arg(projectName_));
}
