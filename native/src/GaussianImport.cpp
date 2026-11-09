#include "MainWindow.hpp"
#include "GaussianSnapshot.hpp"
#include "MoleculeViewWidget.hpp"
#include "si/StructureLabels.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QDirIterator>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

namespace {
QString pairedSinglePointPath(const QString& frequencyPath){return QString::fromStdWString(si::pairedSinglePointPath(std::filesystem::path(frequencyPath.toStdWString())).wstring());}
QString speMatchMessage(si::SpeMatch match){
  switch(match){
  case si::SpeMatch::Verified:return QObject::tr("구조·전하·multiplicity 일치");
  case si::SpeMatch::MissingData:return QObject::tr("최종 구조 또는 SCF 에너지 확인 불가");
  case si::SpeMatch::MissingConditions:return QObject::tr("전하 또는 multiplicity 확인 불가");
  case si::SpeMatch::Atoms:return QObject::tr("원자 수 또는 원소 순서 불일치");
  case si::SpeMatch::Charge:return QObject::tr("전하 불일치");
  case si::SpeMatch::Multiplicity:return QObject::tr("Multiplicity 불일치");
  case si::SpeMatch::Geometry:return QObject::tr("회전·이동 정렬 후 좌표 불일치");
  }
  return {};
}
bool legacySpeAllowed(si::SpeMatch match){
  return si::compatibleSpe(match)||match==si::SpeMatch::MissingData;
}
bool verifiedAutomaticSpe(si::SpeMatch match,const si::GaussianResult& spe){
  return match==si::SpeMatch::Verified&&spe.normalTermination&&!spe.method.empty()&&!spe.basis.empty();
}
std::shared_ptr<const si::GaussianResult> withSpeSpin(const std::shared_ptr<const si::GaussianResult>& primary,
                                                     const si::GaussianResult& spe){
  if(spe.spinDensities.empty())return primary;
  auto combined=std::make_shared<si::GaussianResult>(*primary);
  combined->spinDensities=spe.spinDensities;
  combined->spinBasisLevel=spe.basisLevel;
  combined->spinSourcePath=spe.path;
  combined->spinFallback=false;
  return combined;
}
QString speSearchKey(const QString& path){
  static const QRegularExpression suffix(R"((?:[-_](?:sp|spe|single[-_]?point|bb|opt|freq|(?:def2[-_]?)?(?:tzvpp|svp)))+$)");
  return QFileInfo(path).completeBaseName().toLower().remove(suffix);
}
struct SpeCandidate{
  QString source, path;
  std::shared_ptr<const si::GaussianResult> result;
  si::SpeMatch match;
  bool eligible;
};
QString namedFilePath(const QDir& base,const QString& name,QString* problem=nullptr){
  QString path=base.filePath(name);const auto suffix=QFileInfo(name).suffix();
  if(suffix.compare("out",Qt::CaseInsensitive)!=0&&suffix.compare("log",Qt::CaseInsensitive)!=0){
    const auto out=path+".out",log=path+".log";
    if(QFileInfo(out).isFile()&&QFileInfo(log).isFile()){
      if(problem)*problem=QObject::tr("확장자를 지정하세요 (.out/.log 모두 존재): %1").arg(name);return {};
    }
    path=QFileInfo(out).isFile()?out:log;
  }
  if(QFileInfo(path).isFile())return QFileInfo(path).absoluteFilePath();
  if(problem)*problem=QObject::tr("파일 없음: %1").arg(name);return {};
}
QVector<si::NamedStructureInput> resolvedNamedInputs(const QDir& base,const QString& text,QStringList* problems=nullptr){
  QVector<si::NamedStructureInput> entries,resolved;
  for(const auto& entry:si::namedStructureInputs(text)){
    // Two existing filename cells remain two files, even when the first
    // filename could also be interpreted as a structure label.
    if(entry.implicitPair&&namedFilePath(base,entry.original).isEmpty()&&!namedFilePath(base,entry.label).isEmpty()){
      entries.append({entry.label,entry.label,{}});entries.append({entry.fileName,entry.fileName,{}});
    }else entries.append(entry);
  }
  for(const auto& entry:entries){
    QString problem,label=entry.label;
    QString path=!label.isEmpty()?namedFilePath(base,entry.original):QString();
    if(!path.isEmpty())label.clear();else path=namedFilePath(base,entry.fileName,&problem);
    if(path.isEmpty()){if(problems)problems->append(problem);continue;}
    resolved.append({entry.original,path,label});
  }
  return resolved;
}
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent* event){
  QStringList paths;
  for(const auto& url:event->mimeData()->urls())if(url.isLocalFile()){
    paths.append(url.toLocalFile());
  }
  QStringList files, candidates;
  for(const auto& path:paths){
    if(!QFileInfo(path).isDir()){files.append(path);continue;}
    baseDirectory_->setText(path);
    QDirIterator scan(path,{"*.log","*.out"},QDir::Files|QDir::Readable,QDirIterator::Subdirectories);
    while(scan.hasNext())candidates.append(scan.next());
  }
  candidates.removeDuplicates();
  for(const auto& file:files)candidates.removeAll(file);
  candidates.sort(Qt::CaseInsensitive);
  if(!candidates.isEmpty()){
    QDialog dialog(this);
    dialog.setObjectName("folderGaussianSelection");
    dialog.setWindowTitle(tr("폴더에서 발견한 Gaussian 출력"));
    dialog.resize(760,420);
    auto* layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("가져올 파일을 선택하세요. Ctrl 또는 Shift로 여러 파일을 선택할 수 있습니다."),&dialog));
    auto* list=new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->addItems(candidates);layout->addWidget(list);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    for(const auto* item:list->selectedItems())files.append(item->text());
  }
  if(files.isEmpty())status_->setText(tr("가져올 Gaussian 출력(.log, .out)이 선택되지 않았습니다."));
  else {files.removeDuplicates();parsePaths(files,true);}
  event->acceptProposedAction();
}
void MainWindow::linkSinglePoint(){
  if(parsing_||files_->currentRow()<0)return;
  const QString source=files_->item(files_->currentRow(),0)->data(Qt::UserRole).toString();
  const QString selection=QFileDialog::getOpenFileName(this,tr("Single-point Gaussian Output"),QFileInfo(source).absolutePath(),tr("Gaussian Files (*.out *.log)"));
  if(selection.isEmpty())return;
  si::GaussianParser parser;
  const auto primary=parser.parse(std::filesystem::path(source.toStdWString()),false);
  const auto candidate=parser.parse(std::filesystem::path(selection.toStdWString()),false);
  const auto match=si::validateSpe(primary,candidate);
  if(!si::compatibleSpe(match)||QFileInfo(source)==QFileInfo(selection)){
    QMessageBox::warning(this,tr("SPE 연결 실패"),QFileInfo(source)==QFileInfo(selection)
      ?tr("같은 파일을 SPE로 연결할 수 없습니다."):speMatchMessage(match));
    return;
  }
  const QString details=tr("파일: %1\nMethod: %2\nBasis: %3\nCharge: %4\nMultiplicity: %5\nSCF energy: %6 Hartree\n\n선택 구조와 연결하시겠습니까?")
    .arg(QFileInfo(selection).fileName(),QString::fromStdString(candidate.method),QString::fromStdString(candidate.basis))
    .arg(candidate.charge?QString::number(*candidate.charge):QStringLiteral("N/A"))
    .arg(candidate.multiplicity?QString::number(*candidate.multiplicity):QStringLiteral("N/A"))
    .arg(*candidate.scfEnergy,0,'f',9);
  if(QMessageBox::question(this,tr("SPE 연결 확인"),details+"\n"+speMatchMessage(match))!=QMessageBox::Yes)return;
  const auto absolute=QFileInfo(selection).absoluteFilePath();
  applySinglePoint(source,absolute,primary,std::make_shared<si::GaussianResult>(candidate));
  selectStructure();refreshDocumentModel();
  status_->setText(tr("SPE 수동 연결됨: %1").arg(absolute));
}
void MainWindow::applySinglePoint(const QString& source,const QString& path,const si::GaussianResult& primary,
                                  const std::shared_ptr<const si::GaussianResult>& candidate){
  auto base=std::make_shared<const si::GaussianResult>(primary);
  if(candidate->spinDensities.empty()&&primary.spinSourcePath!=primary.path&&QFileInfo(source).isFile()){
    try{base=cache_->get(source.toStdWString());}
    catch(const std::exception&){/* Keep the primary snapshot if the source disappeared. */}
  }
  manualPairedPaths_.insert(source,path);
  pairedPaths_.insert(source,path);
  parsed_.insert(source,withSpeSpin(base,*candidate));
  parsed_.insert(path,candidate);
}
void MainWindow::reviewSinglePoints(const QString& selectedFolder){
  if(parsing_||files_->rowCount()==0)return;
  const auto paths=sourcePaths();
  const auto folder=selectedFolder.isEmpty()?QFileDialog::getExistingDirectory(this,tr("SPE 후보 폴더"),QFileInfo(paths.first()).absolutePath()):selectedFolder;
  if(folder.isEmpty())return;
  QHash<QString,QStringList> byName;
  for(const auto& path:paths){
    if(manualPairedPaths_.contains(path))continue;
    const auto primary=parsed_.value(path);
    const auto current=parsed_.value(pairedPaths_.value(path));
    if(!primary||!primary->success||(current&&useSinglePoint(path,*primary,*current)))continue;
    byName[speSearchKey(path)].append(path);
  }
  QProgressDialog progress(tr("Gaussian SPE 후보 확인 중…"),QString(),0,0,this);
  progress.setCancelButtonText(tr("취소"));
  progress.setWindowModality(Qt::WindowModal);
  QFutureWatcher<QVector<SpeCandidate>> watcher;
  connect(&watcher,&QFutureWatcherBase::finished,&progress,&QProgressDialog::accept);
  watcher.setFuture(QtConcurrent::run([folder,paths,byName,parsed=parsed_](QPromise<QVector<SpeCandidate>>& promise){
    QVector<SpeCandidate> candidates;
    const auto outputs=QDir(folder).entryInfoList({"*.out","*.log"},QDir::Files);
    for(const auto& output:outputs){
      if(promise.isCanceled())return;
      const auto path=output.absoluteFilePath();
      const auto matches=byName.value(speSearchKey(path));
      if(matches.isEmpty()||paths.contains(path,Qt::CaseInsensitive))continue;
      auto result=std::make_shared<si::GaussianResult>(si::GaussianParser{}.parse(path.toStdWString(),false));
      for(const auto& source:matches){
        const auto match=si::validateSpe(*parsed.value(source),*result);
        const bool eligible=verifiedAutomaticSpe(match,*result)&&result->jobTypes.size()==1&&result->jobTypes.front()=="single_point";
        candidates.append({source,path,result,match,eligible});
      }
    }
    promise.addResult(std::move(candidates));
  }));
  if(!watcher.isFinished()&&progress.exec()!=QDialog::Accepted){
    watcher.cancel();
    return;
  }
  const auto candidates=watcher.result();
  if(candidates.isEmpty()){
    status_->setText(tr("선택한 폴더에 이름이 일치하는 미연결 SPE 후보가 없습니다."));
    return;
  }
  QDialog dialog(this);dialog.setWindowTitle(tr("SPE 후보 검토"));dialog.resize(850,400);
  auto* layout=new QVBoxLayout(&dialog);
  auto* explanation=new QLabel(tr("검증을 통과한 후보를 선택한 뒤 승인하세요. 구조당 파일 하나만 선택할 수 있습니다."),&dialog);
  explanation->setWordWrap(true);layout->addWidget(explanation);
  auto* table=new QTableWidget(candidates.size(),5,&dialog);
  table->setObjectName("speCandidateTable");
  table->setHorizontalHeaderLabels({tr("승인"),tr("구조 파일"),tr("SPE 파일"),tr("Method / Basis"),tr("검증")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setStretchLastSection(true);
  QHash<QString,int> sourceCounts,pathCounts;
  for(const auto& candidate:candidates)if(candidate.eligible){
    ++sourceCounts[candidate.source];++pathCounts[candidate.path];
  }
  for(int row=0;row<candidates.size();++row){
    const auto& candidate=candidates[row];
    auto* approval=new QTableWidgetItem;
    approval->setFlags(candidate.eligible?Qt::ItemIsEnabled|Qt::ItemIsUserCheckable:Qt::NoItemFlags);
    approval->setCheckState(candidate.eligible&&sourceCounts.value(candidate.source)==1&&pathCounts.value(candidate.path)==1
      ?Qt::Checked:Qt::Unchecked);
    table->setItem(row,0,approval);
    const QStringList values={QFileInfo(candidate.source).fileName(),QFileInfo(candidate.path).fileName(),
      QString::fromStdString(candidate.result->method+"/"+candidate.result->basis),
      candidate.match==si::SpeMatch::Verified&&!candidate.eligible?tr("종료 또는 SPE 계산 조건 확인 필요"):speMatchMessage(candidate.match)};
    for(int column=0;column<values.size();++column)table->setItem(row,column+1,new QTableWidgetItem(values[column]));
  }
  layout->addWidget(table);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("선택 연결 승인"));
  layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
    QSet<QString> sources,selectedPaths;
    for(int row=0;row<candidates.size();++row){
      const auto& candidate=candidates[row];
      if(!candidate.eligible||table->item(row,0)->checkState()!=Qt::Checked)continue;
      if(sources.contains(candidate.source)||selectedPaths.contains(candidate.path)){
        QMessageBox::warning(&dialog,tr("중복 연결"),tr("구조와 SPE 파일을 각각 한 번만 선택하세요."));
        return;
      }
      sources.insert(candidate.source);selectedPaths.insert(candidate.path);
    }
    for(int row=0;row<candidates.size();++row){
      const auto& candidate=candidates[row];
      if(candidate.eligible&&table->item(row,0)->checkState()==Qt::Checked)
        applySinglePoint(candidate.source,candidate.path,*parsed_.value(candidate.source),candidate.result);
    }
    selectStructure();refreshDocumentModel();
    status_->setText(tr("SPE %1개 연결 완료").arg(sources.size()));
    dialog.accept();
  });
  dialog.exec();
}
void MainWindow::unlinkSinglePoint(){
  if(parsing_||files_->currentRow()<0)return;
  const QString source=files_->item(files_->currentRow(),0)->data(Qt::UserRole).toString();
  manualPairedPaths_.remove(source);
  restoredSpeLinks_.remove(source);
  const QString automatic=pairedSinglePointPath(source);
  pairedPaths_.remove(source);
  try{
    const auto primary=cache_->get(source.toStdWString());
    parsed_.insert(source,primary);
    if(!automatic.isEmpty()){
      const auto spe=cache_->get(automatic.toStdWString());
      parsed_.insert(automatic,spe);
      pairedPaths_.insert(source,automatic);
      if(useSinglePoint(source,*primary,*spe))parsed_.insert(source,withSpeSpin(primary,*spe));
    }
  }
  catch(const std::exception&){/* Offline project: retain the saved primary snapshot. */}
  selectStructure();refreshDocumentModel();
  status_->setText(tr("수동 SPE 연결 해제 · 자동 매칭 복구"));
}
void MainWindow::chooseFiles(){
  const auto selected=QFileDialog::getOpenFileNames(this,tr("Gaussian 출력 선택 (Select Gaussian Output)"),baseDirectory_->text(),tr("Gaussian Files (*.out *.log);;All Files (*.*)"),nullptr,QFileDialog::DontUseNativeDialog);
  if(!selected.isEmpty()){baseDirectory_->setText(QFileInfo(selected.front()).absolutePath());parsePaths(selected,true);}
}
void MainWindow::chooseDirectory(){
  const auto directory=QFileDialog::getExistingDirectory(this,tr("기본 폴더 선택 (Select Base Folder)"),baseDirectory_->text(),QFileDialog::ShowDirsOnly|QFileDialog::DontUseNativeDialog);
  if(directory.isEmpty())return;
  acceptPaths({directory});
}
void MainWindow::acceptPaths(const QStringList& roots){
  QStringList files;
  for(const auto& root:roots){
    if(QFileInfo(root).isDir())baseDirectory_->setText(QFileInfo(root).absoluteFilePath());
    else files.append(root);
  }
  if(!files.isEmpty())parsePaths(files,true);
  else status_->setText(tr("기본 폴더 설정 완료. 파일 추가에서 불러올 파일을 선택하세요."));
}

void MainWindow::addNamedFiles(bool replace){manualLabelsEdited_=false;
  const QDir base(baseDirectory_->text().trimmed());
  if(baseDirectory_->text().trimmed().isEmpty()||!base.exists()){
    fileNamesInfo_->setText(tr("먼저 기본 폴더를 지정하세요."));return;
  }
  QStringList paths,problems;QHash<QString,QString> replacementLabels;
  for(const auto& entry:resolvedNamedInputs(base,fileNames_->toPlainText(),&problems)){
    const auto& path=entry.fileName;const auto& label=entry.label;
    paths.append(path);
    if(replace){replacementLabels.insert(path,label.isEmpty()?QFileInfo(path).completeBaseName():label);continue;}
    if(!label.isEmpty()){
      bool existing=false;
      for(int row=0;row<files_->rowCount();++row)if(files_->item(row,0)->data(Qt::UserRole).toString().compare(path,Qt::CaseInsensitive)==0){
        files_->item(row,0)->setText(label);existing=true;
        if(files_->currentRow()==row)structureLabel_->setText(label);
        break;
      }
      if(!existing)namedImportLabels_.insert(path.toCaseFolded(),label);
    }
  }
  syncReferenceOptions();scheduleDocumentRefresh();
  fileNamesInfo_->setText(problems.isEmpty()?(paths.isEmpty()?tr("파일명을 붙여넣으세요."):tr("파일명 %1개 확인 완료").arg(paths.size())):problems.join('\n'));
  if(!paths.isEmpty()){if(replace){pendingLabelsByPath_=replacementLabels;parsePaths(paths,false,false);}else parsePaths(paths,true,false);}
}

void MainWindow::parsePaths(const QList<QString>& incoming,bool append,bool discoverIrc,const QString& targetStageId,const QJsonObject& restoredSnapshots,const QJsonObject& restoredPairedPaths){
  const bool restoringProject=(targetStageId==QStringLiteral("@project-restore"));
  const QString destination=targetStageId=="@active"?(activeStage_>=0&&activeStage_<reactionStages_.size()?reactionStages_[activeStage_].toObject().value("id").toString(reactionStages_[activeStage_].toObject()["name"].toString()):QString()):targetStageId;
  if(parsing_&&append){
    queuedImports_.append({incoming,discoverIrc,destination});
    status_->setText(tr("해석 중 — 추가 요청 %1개 대기 중").arg(queuedImports_.size()));
    return;
  }
  // A project replacement invalidates every callback from the previous import.
  // Workers own their cache and never access the window after it is destroyed.
  if(!append){
    if(!restoringProject)manualPairedPaths_.clear();
    manualLabelsEdited_=false;namedImportLabels_.clear();
    ++importGeneration_;
    if(importWatcher_)importWatcher_->cancel();
    importWatcher_=nullptr;parsing_=false;queuedImports_.clear();
  }
  const auto generation=importGeneration_;
  QSet<QString> seen;
  if(append)for(const auto& path:sourcePaths())seen.insert(path.toCaseFolded());
  QList<QString> paths;
  // Only explicit file imports discover the two named IRC endpoints.
  // Project loading must retain the saved list and its user-defined order.
  QStringList expanded;
  QStringList missingEndpoints;
  for(const auto& path:incoming){
    expanded.append(path);
    const QFileInfo info(path);
    if(!append||!discoverIrc||incoming.size()!=1||!info.isFile()||info.completeBaseName().endsWith("-ts-freq",Qt::CaseInsensitive)==false)continue;
    if(info.suffix().compare("out",Qt::CaseInsensitive)!=0&&info.suffix().compare("log",Qt::CaseInsensitive)!=0)continue;
    for(const auto& direction:{QStringLiteral("ircfwd"),QStringLiteral("ircrev")}){
      const auto name=info.completeBaseName()+"-"+direction+"-opt-freq."+info.suffix();
      const auto endpoint=info.dir().filePath(name);
      if(QFileInfo(endpoint).isFile())expanded.append(endpoint);
      else missingEndpoints.append(name);
    }
  }
  for(const auto& path:expanded){
    const QFileInfo info(path);
    const auto absolute=info.absoluteFilePath();
    const bool auxiliary=info.fileName().endsWith("freq-bb.out",Qt::CaseInsensitive)||info.fileName().endsWith("freq-bb.log",Qt::CaseInsensitive);
    if(!auxiliary&&!seen.contains(absolute.toCaseFolded())&&(info.isFile()||restoringProject||(!append&&restoredSnapshots.contains(absolute)))&&(info.suffix().compare("out",Qt::CaseInsensitive)==0||info.suffix().compare("log",Qt::CaseInsensitive)==0)){
      paths.append(absolute);seen.insert(absolute.toCaseFolded());
    }
  }
  if(append&&!destination.isEmpty()){for(int index=0;index<reactionStages_.size();++index){auto stage=reactionStages_[index].toObject();if(stage.value("id").toString(stage["name"].toString())!=destination)continue;auto members=stage["files"].toArray();for(const auto& file:expanded){const auto path=QFileInfo(file).absoluteFilePath();if(seen.contains(path.toCaseFolded())&&!members.contains(path))members.append(path);}stage["files"]=members;if(stage["reference"].toString().isEmpty()&&!members.isEmpty())stage["reference"]=members.first();reactionStages_[index]=stage;break;}}
  if(paths.isEmpty()&&append){refreshStageNavigation();refreshStageWorkspace();scheduleDocumentRefresh();status_->setText(tr("추가할 Gaussian 파일이 없습니다 (중복·보조 파일·경로 확인)."));if(!queuedImports_.isEmpty()){const auto queued=queuedImports_.takeFirst();parsePaths(queued.paths,true,queued.discoverIrc,queued.stageId);}return;}
  const int firstRow=append?files_->rowCount():0;
  const int previousRow=append?files_->currentRow():-1;
  {
    QSignalBlocker blocked(files_);
    files_->setUpdatesEnabled(false);
    if(!append){files_->setRowCount(0);parsed_.clear();pairedPaths_.clear();restoredSpeLinks_.clear();moleculeView_->clearMolecule();}
    files_->setRowCount(firstRow+paths.size());
    for(int i=0;i<paths.size();++i){
      const auto& path=paths[i];const int row=firstRow+i;
      const auto importedLabel=append?namedImportLabels_.take(path.toCaseFolded()):QString();
      const auto label=append?(importedLabel.isEmpty()?QFileInfo(path).completeBaseName():importedLabel):pendingLabelsByPath_.value(path,QFileInfo(path).completeBaseName());
      files_->setItem(row,0,new QTableWidgetItem(label));files_->item(row,0)->setData(Qt::UserRole,path);
      for(int column=1;column<7;++column)files_->setItem(row,column,new QTableWidgetItem);
      files_->item(row,6)->setText(tr("대기 중…"));
    }
    files_->setUpdatesEnabled(true);
  }
  if(!append){pendingLabelsByPath_.clear();pendingStructureLabels_.clear();}
  refreshStageNavigation();refreshStageWorkspace();syncReferenceOptions();
  if(paths.isEmpty()){scheduleDocumentRefresh();status_->setText(tr("구조 목록이 비었습니다."));return;}
  parsing_=true;previewTimer_->stop();
  status_->setText(tr("0 / %1개 구조 해석 중…").arg(paths.size()));
  struct Result{QString path;std::shared_ptr<const si::GaussianResult> data;QString pairPath;std::shared_ptr<const si::GaussianResult> pair;bool restoredPair{};};
  auto* watcher=new QFutureWatcher<Result>(this);importWatcher_=watcher;
  connect(watcher,&QFutureWatcher<Result>::resultReadyAt,this,[this,watcher,generation](int index){
    if(generation!=importGeneration_)return;
    const auto item=watcher->resultAt(index);
    // The user may have removed a pending row while the worker was reading it.
    for(int row=0;row<files_->rowCount();++row)if(files_->item(row,0)->data(Qt::UserRole).toString()==item.path){
      parsed_.insert(item.path,item.data);pairedPaths_.insert(item.path,item.pairPath);
      if(item.restoredPair)restoredSpeLinks_.insert(item.path);
      if(item.pair)parsed_.insert(item.pairPath,item.pair);
      files_->item(row,6)->setText(item.data->success?tr("해석 완료"):tr("Error"));
      files_->item(row,6)->setToolTip(QString::fromStdString(item.data->reason));
      if(files_->currentRow()==row)selectStructure();
      break;
    }
    status_->setText(tr("%1 / %2개 구조 해석 완료…").arg(index+1).arg(watcher->future().progressMaximum()));
  });
  connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,generation,previousRow,missingEndpoints]{
    watcher->deleteLater();
    if(generation!=importGeneration_)return;
    importWatcher_=nullptr;parsing_=false;
    refreshPreflight();
    if(files_->rowCount()>0){QSignalBlocker blocked(files_);files_->selectRow(previousRow>=0?std::min(previousRow,files_->rowCount()-1):0);selectStructure();}
    status_->setText(tr("구조 %1개를 불러왔습니다.").arg(files_->rowCount()));
    if(!missingEndpoints.isEmpty())status_->setText(status_->text()+tr("\n연결 IRC 파일 없음: %1").arg(missingEndpoints.join(", ")));
    emit files_->itemSelectionChanged();
    if(!queuedImports_.isEmpty()){
      const auto queued=queuedImports_.takeFirst();parsePaths(queued.paths,true,queued.discoverIrc,queued.stageId);
    }
    scheduleDocumentRefresh();
  });
  const auto cache=cache_;
  watcher->setFuture(QtConcurrent::run([cache,paths,restoredSnapshots,restoredPairedPaths](QPromise<Result>& promise){
    promise.setProgressRange(0,paths.size());
    auto parse=[cache,&restoredSnapshots](const QString& path)->std::shared_ptr<const si::GaussianResult>{
      // Re-read accessible Gaussian outputs with the current parser. Older
      // project snapshots may contain partial hydrogen-summed spin data.
      // The saved snapshot remains a fallback for portable/offline projects.
      if(QFileInfo::exists(path)){
        try{
          const auto fresh=cache->get(path.toStdWString());
          if(fresh&&fresh->success)return fresh;
        }catch(const std::exception&){/* Fall back to the stored snapshot. */}
      }
      if(const auto packed=restoredSnapshots.value(path);packed.isObject()){
        si::GaussianResult saved;
        if(si::GaussianSnapshot::decode(packed.toObject(),&saved))
          return std::make_shared<const si::GaussianResult>(std::move(saved));
      }
      try{return cache->get(path.toStdWString());}
      catch(const std::exception& error){auto failed=std::make_shared<si::GaussianResult>();failed->path=path.toStdWString();failed->success=false;failed->reason=error.what();return failed;}
    };
    for(int i=0;i<paths.size();++i){
      if(promise.isCanceled())return;
      const auto& path=paths[i];Result item{path,parse(path),{},{}};
      try{
        item.pairPath=restoredPairedPaths.value(path).toString();
        item.restoredPair=!item.pairPath.isEmpty();
        if(item.pairPath.isEmpty())item.pairPath=pairedSinglePointPath(path);
        if(!item.pairPath.isEmpty()){
           item.pair=parse(item.pairPath);
           if(item.pair&&item.pair->success&&item.data){
             const auto match=si::validateSpe(*item.data,*item.pair);
             const bool usable=item.restoredPair?legacySpeAllowed(match):verifiedAutomaticSpe(match,*item.pair);
             if(usable)item.data=withSpeSpin(item.data,*item.pair);
             else if(item.data->spinSourcePath==item.pair->path){
               auto primary=std::make_shared<si::GaussianResult>(*item.data);
               primary->spinDensities.clear();
               primary->spinSourcePath=primary->path;
               item.data=primary;
             }
           }
         }
      }
      catch(const std::exception&){/* Primary parse already reports filesystem failures. */}
      if(promise.isCanceled())return;
      promise.addResult(std::move(item));promise.setProgressValue(i+1);
    }
  }));
}
void MainWindow::syncReferenceOptions(){
  const QString previous=pendingReferencePath_.isEmpty()?reference_->currentData().toString():pendingReferencePath_;
  QSignalBlocker blocked(reference_);reference_->clear();
  for(int row=0;row<files_->rowCount();++row)if(auto* item=files_->item(row,0))reference_->addItem(item->text(),item->data(Qt::UserRole));
  const int index=reference_->findData(previous);
  reference_->setCurrentIndex(index>=0?index:(reference_->count()?0:-1));
  pendingReferencePath_.clear();
}
bool MainWindow::useSinglePoint(const QString& source,const si::GaussianResult& primary,const si::GaussianResult& spe) const{
  if(!spe.success||!spe.scfEnergy)return false;
  const auto match=si::validateSpe(primary,spe);
  if(restoredSpeLinks_.contains(source)||manualPairedPaths_.contains(source))return legacySpeAllowed(match);
  return verifiedAutomaticSpe(match,spe);
}

std::vector<si::EnergyRecord> MainWindow::energyRecords() const{
  std::vector<si::EnergyRecord> records;
  for(int row=0;row<files_->rowCount();++row){
    const auto* item=files_->item(row,0);if(!item)continue;
    const auto path=item->data(Qt::UserRole).toString();const auto main=parsed_.value(path);if(!main)continue;
    si::EnergyRecord record;record.name=item->text().toStdString();record.frequency=*main;
    if(const auto pair=parsed_.value(pairedPaths_.value(path));pair&&useSinglePoint(path,*main,*pair)){record.singlePoint=*pair;if(record.frequency.finalGeometry.empty()&&!pair->finalGeometry.empty())record.frequency.finalGeometry=pair->finalGeometry;}
    records.push_back(std::move(record));
  }
  return records;
}
void MainWindow::refreshPreflight(){
  auto records=energyRecords();geometryAutoInfo_->clear();
  const auto basis=static_cast<si::EnergyBasis>(energyBasis_->currentData().toInt());
  QStringList conditionWarnings;
  (void)si::computationalDetails(records,basis,&conditionWarnings);
  const auto paths=sourcePaths();
  for(auto& record:records){
    const int row=paths.indexOf(QDir::fromNativeSeparators(QString::fromStdWString(record.frequency.path.wstring())));
    if(row<0)continue;const auto& freq=record.frequency;QStringList errors,warnings=conditionWarnings;
    if(!freq.success)errors.append(QString::fromStdString(freq.reason));
    if(freq.finalGeometry.empty())errors.append("Final geometry missing");
    if(energyInputMode_->currentData().toString()=="automatic"&&!si::selectEnergy(record,basis))errors.append("Required energy missing for selected mode");
    if(!freq.normalTermination)warnings.append("Normal termination not confirmed");
    if(!freq.frequenciesPresent)warnings.append("Frequency data missing");
    if(!record.singlePoint||!record.singlePoint->scfEnergy)warnings.append("SPE missing");
    if(const auto spe=parsed_.value(pairedPaths_.value(paths[row]))){
      const auto match=si::validateSpe(freq,*spe);
      if(match!=si::SpeMatch::Verified)warnings.append(speMatchMessage(match));
      if(match==si::SpeMatch::Charge&&freq.multiplicity&&spe->multiplicity&&freq.multiplicity!=spe->multiplicity)
        warnings.append(speMatchMessage(si::SpeMatch::Multiplicity));
      if(!spe->normalTermination)warnings.append("SPE normal termination not confirmed");
      if(spe->method.empty()||spe->basis.empty())warnings.append("SPE method or basis missing");
    }
    if(freq.imaginaryFrequencies.size()>=2)warnings.append("2+ imaginary frequencies");
    if(freq.spinDensities.empty())warnings.append("Spin density missing");
    const QStringList values={freq.multiplicity?QString::number(*freq.multiplicity):QStringLiteral("N/A"),
      freq.frequenciesPresent?QStringLiteral("✓"):QStringLiteral("—"),
      record.singlePoint&&record.singlePoint->scfEnergy?QStringLiteral("✓"):QStringLiteral("—"),
      freq.frequenciesPresent?QString::number(freq.imaginaryFrequencies.size()):QStringLiteral("N/A"),
      freq.spinDensities.empty()?QStringLiteral("—"):QStringLiteral("✓"),
      !errors.isEmpty()?QStringLiteral("Error"):!warnings.isEmpty()?QStringLiteral("Warning"):QStringLiteral("Ready")};
    for(int column=1;column<7;++column){files_->item(row,column)->setText(values[column-1]);files_->item(row,column)->setToolTip((errors+warnings).join('\n'));}
    files_->item(row,6)->setData(Qt::UserRole,errors);
  }
  updateStructureStatusCells();
}

void MainWindow::applyNamedLabelsForExport(){
  if(manualLabelsEdited_&&labelInputAtManualEdit_==fileNames_->toPlainText()&&labelBaseAtManualEdit_==baseDirectory_->text())return;
  if(parsing_||fileNames_->toPlainText().trimmed().isEmpty()||baseDirectory_->text().trimmed().isEmpty())return;
  const QDir base(baseDirectory_->text().trimmed());if(!base.exists())return;
  QHash<QString,QString> labels;
  for(const auto& entry:resolvedNamedInputs(base,fileNames_->toPlainText()))
    if(!entry.label.isEmpty())labels.insert(entry.fileName.toCaseFolded(),entry.label);
  {
    QSignalBlocker blocked(files_);
    for(int row=0;row<files_->rowCount();++row){
      auto* item=files_->item(row,0);if(!item)continue;
      const auto label=labels.value(item->data(Qt::UserRole).toString().toCaseFolded());
      if(!label.isEmpty())item->setText(label);
    }
  }
  syncReferenceOptions();
  if(auto* item=files_->item(files_->currentRow(),0))structureLabel_->setText(item->text());
}
