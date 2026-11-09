#include <QTextEdit>
#include <QApplication>
#include "MainWindow.hpp"
#include "DocxWriter.hpp"
#include "FigureSetupDialog.hpp"
#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"
#include "si/XyzWriter.hpp"

#include <QFileDialog>
#include <QCheckBox>
#include <QDesktopServices>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#endif

bool MainWindow::prepareFigures(bool preflightDone,bool interactive){
  if(!preflightDone&&!validateExport())return false;
  if(activeStage_<0&&!reactionStages_.isEmpty()){const int previous=activeStage_;for(int i=0;i<reactionStages_.size();++i){if(reactionStages_[i].toObject()["files"].toArray().isEmpty())continue;activeStage_=i;if(!prepareFigures(true,interactive)){activeStage_=previous;refreshStageWorkspace();return false;}}activeStage_=previous;refreshStageWorkspace();refreshScopedPreview();return true;}
  if(documentModel_.coordinates.isEmpty()){QMessageBox::information(this,tr("분자 없음"),tr("먼저 Gaussian 구조를 추가하세요."));return false;}
  for(const auto& block:documentModel_.coordinates)if(block.atoms.empty()){QMessageBox::warning(this,tr("좌표 없음"),block.structureName+tr(": 렌더링할 좌표가 없습니다."));return false;}
  const bool selectedStage=activeStage_>=0&&activeStage_<reactionStages_.size();
  auto stage=selectedStage?reactionStages_[activeStage_].toObject():QJsonObject();
  auto coordinates=documentModel_.coordinates;auto paths=sourcePaths();
  if(selectedStage){coordinates.clear();paths.clear();const auto allPaths=sourcePaths();for(const auto& file:stage["files"].toArray()){const int index=allPaths.indexOf(file.toString());if(index>=0&&index<documentModel_.coordinates.size()){coordinates.append(documentModel_.coordinates[index]);paths.append(file.toString());}}if(coordinates.isEmpty()){status_->setText(tr("현재 반응에 구조를 먼저 추가하세요."));return false;}}
  auto settings=selectedStage?stage["figure_settings"].toObject():figureSettings_;
  if(!settings.contains("captions")){
    const auto text=selectedStage?stage["figure_captions"].toString():captions_->toPlainText();
    QJsonArray captions;
    for(const auto& line:text.split('\n',Qt::SkipEmptyParts))
      if(!line.trimmed().isEmpty())captions.append(line.trimmed());
    settings["captions"]=captions;
  }
  FigureSetupDialog dialog(coordinates,paths,settings,this);
  if(selectedStage)dialog.setWindowTitle(stage["name"].toString()+tr(" — Figure 구성"));
  if(interactive){
    if(dialog.exec()!=QDialog::Accepted)return false;
  }else {dialog.renderFigures();}
  if(selectedStage){if(interactive){stage["figure_settings"]=dialog.settings();reactionStages_[activeStage_]=stage;}preparedStageFigures_[stage.value("id").toString(stage["name"].toString())]=dialog.figures();}else {if(interactive)figureSettings_=dialog.settings();preparedFigures_=dialog.figures();}refreshDocumentModel();return true;
}
bool MainWindow::prepareSiExport(){
  if(generationRunning_)return false;
  applyNamedLabelsForExport();
  if(!validateExport())return false;
  if(documentModel_.empty()){
    QMessageBox::warning(this,tr("입력 없음"),tr("먼저 Gaussian 파일이나 수동 표를 추가하세요."));
    return false;
  }
  if(exportFigures_->isChecked()&&!documentModel_.coordinates.isEmpty()){
    const int previous=activeStage_;
    activeStage_=-1;
    const bool prepared=prepareFigures(true,false);
    activeStage_=previous;refreshDocumentModel();
    if(!prepared)return false;
  }
  return true;
}

QString MainWindow::xyzPathForDocx(const QString& path){
  const QFileInfo docx(path);
  return docx.absoluteDir().filePath(docx.completeBaseName()+".xyz");
}

void MainWindow::exportSiDocument(){
  if(!prepareSiExport())return;
  if(archiveExport_->isChecked()&&currentProjectPath_.isEmpty()){
    saveProject();
    if(currentProjectPath_.isEmpty())return;
  }
  auto model=documentModel_;
  if(!exportFigures_->isChecked()){model.figures.clear();model.figureCaptions.clear();}
  const bool openInWord=openExportInWord_->isChecked();
  const bool archive=archiveExport_->isChecked();
  QString path=siOutputPath_->text().trimmed();
  if(path.isEmpty())path=QFileDialog::getSaveFileName(this,tr("SI 문서 생성 (Generate SI)"),baseDirectory_->text()+QStringLiteral("/SI.docx"),tr("Word Document (*.docx)"));
  if(path.isEmpty())return;
  if(!path.endsWith(QStringLiteral(".docx"),Qt::CaseInsensitive))path+=QStringLiteral(".docx");
  const auto xyz=xyzPathForDocx(path);
  siOutputPath_->setText(path);xyzOutputPath_->setText(xyz);
  generationRunning_=true;
  if(auto* button=findChild<QToolButton*>(QStringLiteral("generateSiButton")))button->setEnabled(false);
  status_->setText(tr("SI DOCX + XYZ 생성 중…"));
  const auto historyPath=currentProjectPath_;
  const auto historyState=historyPath.isEmpty()?QJsonObject():ProjectStore::serialize(historyPath,captureProjectState());
  const auto historyMessage=tr("SI 내보내기 · %1").arg(QFileInfo(path).fileName());
  struct GenerationResult {bool docx{},xyz{};QString docxError,xyzError,historyError;};
  auto* watcher=new QFutureWatcher<GenerationResult>(this);
  connect(watcher,&QFutureWatcher<GenerationResult>::finished,this,[this,watcher,path,xyz,openInWord]{
    const auto result=watcher->result();
    watcher->deleteLater();generationRunning_=false;
    if(auto* button=findChild<QToolButton*>(QStringLiteral("generateSiButton")))button->setEnabled(true);
    if(result.docx&&result.xyz){
      status_->setText(tr("SI 문서 생성 완료: %1 · XYZ 생성 완료: %2").arg(path,xyz));

    }else{
      status_->setText(tr("생성 결과 · DOCX: %1 · XYZ: %2").arg(result.docx?tr("성공"):tr("실패"),result.xyz?tr("성공"):tr("실패")));
      QMessageBox::warning(this,tr("SI + XYZ 생성 결과"),
        tr("DOCX: %1\nXYZ: %2").arg(result.docx?tr("생성 완료"):result.docxError,result.xyz?tr("생성 완료"):result.xyzError));
    }
    if(!result.historyError.isEmpty())QMessageBox::warning(this,tr("버전 기록 실패"),result.historyError);
    if(!result.docx||!openInWord)return;
#ifdef Q_OS_WIN
    const auto arguments=(QStringLiteral("\"")+QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())+QStringLiteral("\"")).toStdWString();
    const bool opened=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",L"winword.exe",arguments.c_str(),nullptr,SW_SHOWNORMAL))>32;
#else
    const bool opened=QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
#endif
    if(!opened)QMessageBox::warning(this,tr("문서 열기 실패"),tr("생성한 SI 문서를 열 수 없습니다.\n%1").arg(path));
  });
  watcher->setFuture(QtConcurrent::run([path,xyz,model,historyPath,historyState,historyMessage,archive]{
    GenerationResult result;
    result.docx=DocxWriter::writeDocument(path,model,&result.docxError);
    result.xyz=si::XyzWriter::write(xyz,model,&result.xyzError);
    if(result.docx&&(result.xyz||archive)&&!historyPath.isEmpty())
      ProjectHistory(historyPath).record(historyState,historyMessage,&result.historyError,archive?path:QString());
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

void MainWindow::previewWordPages(){
#ifdef Q_OS_WIN
  if(!prepareSiExport())return;
  auto model=documentModel_;
  if(!exportFigures_->isChecked()){model.figures.clear();model.figureCaptions.clear();}
  const auto draft=std::make_shared<QTemporaryDir>();
  if(!draft->isValid()){
    QMessageBox::warning(this,tr("페이지 미리보기 실패"),draft->errorString());
    return;
  }
  const auto path=draft->filePath("SI-preview.docx");
  auto* watcher=new QFutureWatcher<QString>(this);
  connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,draft,path]{
    const auto error=watcher->result();
    watcher->deleteLater();
    if(!error.isEmpty())QMessageBox::warning(this,tr("페이지 미리보기 실패"),error);
    else runWordOperation(this,path,{},draft);
  });
  watcher->setFuture(QtConcurrent::run([draft,path,model]{
    QString error;
    DocxWriter::writeDocument(path,model,&error);
    return error;
  }));
#endif
}

void MainWindow::runWordOperation(QWidget* parent,const QString& source,const QString& revised,std::shared_ptr<QTemporaryDir> draft){
#ifdef Q_OS_WIN
  const bool compare=!revised.isEmpty();
  const auto title=compare?tr("Word 문서 비교"):tr("Word 페이지 미리보기");
  const auto output=std::make_shared<QTemporaryDir>();
  if(!output->isValid()){QMessageBox::warning(parent,title,output->errorString());return;}
  const auto path=output->filePath(compare?"SI-comparison.docx":"SI-preview.pdf");
  auto* process=new QProcess(parent);
  auto environment=QProcessEnvironment::systemEnvironment();
  environment.insert("SI_WORD_SOURCE",QDir::toNativeSeparators(source));
  environment.insert("SI_WORD_REVISED",QDir::toNativeSeparators(revised));
  environment.insert("SI_WORD_OUTPUT",QDir::toNativeSeparators(path));
  process->setProcessEnvironment(environment);
  const auto script=QStringLiteral(R"(
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$word = New-Object -ComObject Word.Application
try {
  $word.DisplayAlerts = 0
  $word.AutomationSecurity = 3
  $readOnly = [bool]$env:SI_WORD_REVISED
  $base = $word.Documents.Open($env:SI_WORD_SOURCE, $false, $readOnly, $false)
  if ($env:SI_WORD_REVISED) {
    $revised = $word.Documents.Open($env:SI_WORD_REVISED, $false, $true, $false)
    $comparison = $word.CompareDocuments($base, $revised)
    $comparison.SaveAs2($env:SI_WORD_OUTPUT, 12)
  } else {
    $base.ExportAsFixedFormat($env:SI_WORD_OUTPUT, 17)
  }
} finally {
  $word.Quit([ref]0)
}
)");
  connect(process,&QProcess::errorOccurred,parent,[process,parent,title](QProcess::ProcessError error){
    if(error!=QProcess::FailedToStart)return;
    QMessageBox::warning(parent,title,process->errorString());
    process->deleteLater();
  });
  connect(process,&QProcess::finished,parent,[process,parent,title,path,output,draft](int code,QProcess::ExitStatus status){
    const auto details=QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    process->deleteLater();
    if(code!=0||status!=QProcess::NormalExit||!QFileInfo::exists(path)){
      QMessageBox::warning(parent,title,tr("Word에서 문서를 처리하지 못했습니다. Word가 설치되어 있는지 확인하세요.\n%1").arg(details));
      return;
    }
    // Keep the PDF or comparison document available to the viewer for this session.
    connect(qApp,&QCoreApplication::aboutToQuit,qApp,[output]{});
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
      QMessageBox::warning(parent,title,tr("결과 파일을 열 수 없습니다.\n%1").arg(path));
  });
  QByteArray utf16;
  utf16.reserve(script.size()*2);
  for(const auto character:script){
    utf16.append(char(character.unicode()&0xff));
    utf16.append(char(character.unicode()>>8));
  }
  process->start("powershell.exe",{"-NoProfile","-NonInteractive","-STA","-OutputFormat","Text","-EncodedCommand",QString::fromLatin1(utf16.toBase64())});
#else
  Q_UNUSED(parent);Q_UNUSED(source);Q_UNUSED(revised);Q_UNUSED(draft);
#endif
}
