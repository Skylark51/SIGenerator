#include "MainWindow.hpp"
#include "DocxWriter.hpp"
#include "FigureSetupDialog.hpp"
#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"
#include "si/XyzWriter.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>

bool MainWindow::prepareFigures(bool preflightDone){
  if(!preflightDone&&!validateExport())return false;
  if(activeStage_<0&&!reactionStages_.isEmpty()){const int previous=activeStage_;for(int i=0;i<reactionStages_.size();++i){if(reactionStages_[i].toObject()["files"].toArray().isEmpty())continue;activeStage_=i;if(!prepareFigures(true)){activeStage_=previous;refreshStageWorkspace();return false;}}activeStage_=previous;refreshStageWorkspace();refreshScopedPreview();return true;}
  if(documentModel_.coordinates.isEmpty()){QMessageBox::information(this,tr("분자 없음"),tr("먼저 Gaussian 구조를 추가하세요."));return false;}
  for(const auto& block:documentModel_.coordinates)if(block.atoms.empty()){QMessageBox::warning(this,tr("좌표 없음"),block.structureName+tr(": 렌더링할 좌표가 없습니다."));return false;}
  const bool selectedStage=activeStage_>=0&&activeStage_<reactionStages_.size();
  auto stage=selectedStage?reactionStages_[activeStage_].toObject():QJsonObject();
  auto coordinates=documentModel_.coordinates;auto paths=sourcePaths();
  if(selectedStage){coordinates.clear();paths.clear();const auto allPaths=sourcePaths();for(const auto& file:stage["files"].toArray()){const int index=allPaths.indexOf(file.toString());if(index>=0&&index<documentModel_.coordinates.size()){coordinates.append(documentModel_.coordinates[index]);paths.append(file.toString());}}if(coordinates.isEmpty()){status_->setText(tr("현재 반응에 구조를 먼저 추가하세요."));return false;}}
  FigureSetupDialog dialog(coordinates,paths,selectedStage?stage["figure_settings"].toObject():figureSettings_,this);
  if(selectedStage)dialog.setWindowTitle(stage["name"].toString()+tr(" — Figure 구성"));
  if(dialog.exec()!=QDialog::Accepted)return false;
  if(selectedStage){stage["figure_settings"]=dialog.settings();reactionStages_[activeStage_]=stage;preparedStageFigures_[stage.value("id").toString(stage["name"].toString())]=dialog.figures();}else {figureSettings_=dialog.settings();preparedFigures_=dialog.figures();}refreshDocumentModel();return true;
}
void MainWindow::exportSiDocument(){if(generationRunning_)return;applyNamedLabelsForExport();if(!validateExport())return;
  if(documentModel_.empty()){QMessageBox::warning(this,tr("입력 없음 (No Input)"),tr("먼저 Gaussian 파일이나 수동 표를 추가하세요."));return;}
  QMessageBox rendering(QMessageBox::Question,tr("SI Figure 렌더링"),
    tr("SI에 분자 Figure를 렌더링하여 포함하시겠습니까?\n표만 생성하면 Figure와 Figure 캡션을 제외합니다."),QMessageBox::NoButton,this);
  rendering.setObjectName("siRenderingChoice");
  auto* includeFigures=rendering.addButton(tr("렌더링 포함"),QMessageBox::YesRole);
  auto* tablesOnly=rendering.addButton(tr("Figure 없이 표만 생성"),QMessageBox::NoRole);
  auto* cancel=rendering.addButton(QMessageBox::Cancel);
  rendering.setDefaultButton(tablesOnly);rendering.setEscapeButton(cancel);rendering.exec();
  if(rendering.clickedButton()!=includeFigures&&rendering.clickedButton()!=tablesOnly)return;
  const bool renderFigures=rendering.clickedButton()==includeFigures;
  if(renderFigures&&!documentModel_.coordinates.isEmpty()){
    if(reactionStages_.isEmpty()){if(!prepareFigures(true))return;}
    else {const int previous=activeStage_;for(int i=0;i<reactionStages_.size();++i){const auto stage=reactionStages_[i].toObject();if(stage["files"].toArray().isEmpty())continue;const auto id=stage.value("id").toString(stage["name"].toString());if(preparedStageFigures_.contains(id))continue;activeStage_=i;if(!prepareFigures(true)){activeStage_=previous;refreshStageWorkspace();refreshScopedPreview();return;}}activeStage_=previous;refreshStageWorkspace();refreshDocumentModel();}
  }
  auto model=documentModel_;
  if(!renderFigures){model.figures.clear();model.figureCaptions.clear();}
  QString path=siOutputPath_->text().trimmed();
  if(path.isEmpty())path=QFileDialog::getSaveFileName(this,tr("SI 문서 생성 (Generate SI)"),baseDirectory_->text()+QStringLiteral("/SI.docx"),tr("Word Document (*.docx)"));
  if(path.isEmpty())return;
  if(!path.endsWith(QStringLiteral(".docx"),Qt::CaseInsensitive))path+=QStringLiteral(".docx");
  QString xyz=xyzOutputPath_->text().trimmed();
  if(xyz.isEmpty())xyz=QFileDialog::getSaveFileName(this,tr("XYZ 출력 경로"),QFileInfo(path).absoluteDir().filePath("Structures.xyz"),tr("XYZ coordinates (*.xyz)"));
  if(xyz.isEmpty())return;
  if(!xyz.endsWith(QStringLiteral(".xyz"),Qt::CaseInsensitive))xyz+=QStringLiteral(".xyz");
  siOutputPath_->setText(path);xyzOutputPath_->setText(xyz);
  generationRunning_=true;
  if(auto* button=findChild<QToolButton*>(QStringLiteral("generateSiButton")))button->setEnabled(false);
  status_->setText(tr("SI DOCX + XYZ 생성 중…"));
  const auto historyPath=currentProjectPath_;
  const auto historyState=historyPath.isEmpty()?QJsonObject():ProjectStore::serialize(historyPath,captureProjectState());
  const auto historyMessage=tr("SI 내보내기 · %1").arg(QFileInfo(path).fileName());
  struct GenerationResult {bool docx{},xyz{};QString docxError,xyzError,historyError;};
  auto* watcher=new QFutureWatcher<GenerationResult>(this);
  connect(watcher,&QFutureWatcher<GenerationResult>::finished,this,[this,watcher,path,xyz]{
    const auto result=watcher->result();
    watcher->deleteLater();generationRunning_=false;
    if(auto* button=findChild<QToolButton*>(QStringLiteral("generateSiButton")))button->setEnabled(true);
    if(result.docx&&result.xyz){
      status_->setText(tr("SI 문서 생성 완료: %1 · XYZ 생성 완료: %2").arg(path,xyz));
      if(!result.historyError.isEmpty())QMessageBox::warning(this,tr("버전 기록 실패"),result.historyError);
    }else{
      status_->setText(tr("생성 결과 · DOCX: %1 · XYZ: %2").arg(result.docx?tr("성공"):tr("실패"),result.xyz?tr("성공"):tr("실패")));
      QMessageBox::warning(this,tr("SI + XYZ 생성 결과"),
        tr("DOCX: %1\nXYZ: %2").arg(result.docx?tr("생성 완료"):result.docxError,result.xyz?tr("생성 완료"):result.xyzError));
    }
  });
  watcher->setFuture(QtConcurrent::run([path,xyz,model,historyPath,historyState,historyMessage]{
    GenerationResult result;
    result.docx=DocxWriter::writeDocument(path,model,&result.docxError);
    result.xyz=si::XyzWriter::write(xyz,model,&result.xyzError);
    if(result.docx&&result.xyz&&!historyPath.isEmpty())
      ProjectHistory(historyPath).record(historyState,historyMessage,&result.historyError);
    return result;
  }));
}
void MainWindow::exportCoordinatesDocument(){if(!validateExport())return;if(documentModel_.coordinates.isEmpty()){QMessageBox::warning(this,tr("좌표 없음 (No Coordinates)"),tr("최종 Gaussian geometry가 없습니다."));return;}QString path=QFileDialog::getSaveFileName(this,tr("Generate Coordinates DOCX"),baseDirectory_->text()+QStringLiteral("/Coordinates.docx"),tr("Word Document (*.docx)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".docx"),Qt::CaseInsensitive))path+=QStringLiteral(".docx");const auto model=documentModel_;status_->setText(tr("Cartesian DOCX 생성 중…"));auto* watcher=new QFutureWatcher<QString>(this);connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,path]{const auto error=watcher->result();watcher->deleteLater();if(error.isEmpty())status_->setText(tr("Cartesian 문서 생성 완료: %1").arg(path));else QMessageBox::critical(this,tr("문서 오류 (Document Error)"),error);});watcher->setFuture(QtConcurrent::run([path,model]{QString error;DocxWriter::writeCoordinates(path,model,&error);return error;}));}

void MainWindow::exportXyzDocument(){
  if(!validateExport())return;
  if(documentModel_.coordinates.isEmpty()){QMessageBox::warning(this,tr("No Coordinates"),tr("최종 Gaussian geometry가 없습니다."));return;}
  QString path=xyzOutputPath_->text().trimmed();
  if(path.isEmpty())path=QFileDialog::getSaveFileName(this,tr("Generate XYZ"),baseDirectory_->text()+"/Structures.xyz",tr("XYZ coordinates (*.xyz)"));
  if(path.isEmpty())return;
  if(!path.endsWith(".xyz",Qt::CaseInsensitive))path+=".xyz";
  QString error;
  if(!si::XyzWriter::write(path,documentModel_,&error)){QMessageBox::critical(this,tr("XYZ Error"),error);return;}
  xyzOutputPath_->setText(path);status_->setText(tr("XYZ 생성 완료: %1").arg(path));
}
