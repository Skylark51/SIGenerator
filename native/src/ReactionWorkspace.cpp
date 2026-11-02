#include "MainWindow.hpp"
#include "StructureAssignmentTable.hpp"
#include "DocumentPreviewWidget.hpp"
#include "MoleculeViewWidget.hpp"
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QTextEdit>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString stageId(const QJsonObject& stage){return stage.value("id").toString(stage["name"].toString());}
const QStringList tableKeys={"absolute","relative","spin","geometry"};
}

QWidget* MainWindow::createStageNavigation(){
  auto* panel=new QWidget(this);panel->setObjectName("stageNavigator");panel->setMinimumWidth(175);panel->setMaximumWidth(310);auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(10,12,8,10);
  projectHeading_=new QLabel(tr("SI 프로젝트"),panel);projectHeading_->setWordWrap(true);projectHeading_->setStyleSheet("font-weight:600;font-size:12pt;color:#24486c");layout->addWidget(projectHeading_);
  auto* all=new QPushButton(tr("전체 SI"),panel);all->setObjectName("showFullSi");layout->addWidget(all);connect(all,&QPushButton::clicked,this,[this]{chooseStageScope(-1);});
  layout->addWidget(new QLabel(tr("반응 단계 · 드래그로 순서 변경"),panel));
  stageNavigation_=new QListWidget(panel);stageNavigation_->setObjectName("stageNavigation");stageNavigation_->setDragDropMode(QAbstractItemView::InternalMove);stageNavigation_->setDefaultDropAction(Qt::MoveAction);stageNavigation_->setSpacing(5);stageNavigation_->setWordWrap(true);stageNavigation_->setStyleSheet("QListWidget {background:transparent;border:0;} QListWidget::item {padding:12px 8px;border:1px solid #d8e1ec;border-radius:7px;background:white;} QListWidget::item:selected {background:#dceaff;color:#24486c;border-color:#7da6db;}");layout->addWidget(stageNavigation_,1);
  connect(stageNavigation_,&QListWidget::currentRowChanged,this,[this](int row){if(row>=0)chooseStageScope(row);});
  connect(stageNavigation_->model(),&QAbstractItemModel::rowsMoved,this,[this]{QJsonArray ordered;for(int i=0;i<stageNavigation_->count();++i){const auto id=stageNavigation_->item(i)->data(Qt::UserRole).toString();for(const auto& value:reactionStages_)if(stageId(value.toObject())==id){ordered.append(value);break;}}if(ordered.size()!=reactionStages_.size())return;reactionStages_=ordered;activeStage_=stageNavigation_->currentRow();refreshStageWorkspace();refreshScopedPreview();scheduleDocumentRefresh();});
  auto* add=new QPushButton(tr("＋ 반응 추가"),panel);add->setObjectName("addReactionStage");layout->addWidget(add);connect(add,&QPushButton::clicked,this,[this]{addReactionStage();});
  auto* duplicate=new QPushButton(tr("선택 반응 복제"),panel);layout->addWidget(duplicate);connect(duplicate,&QPushButton::clicked,this,[this]{addReactionStage(true);});
  auto* remove=new QPushButton(tr("선택 반응 삭제"),panel);layout->addWidget(remove);connect(remove,&QPushButton::clicked,this,[this]{if(activeStage_<0||activeStage_>=reactionStages_.size())return;if(QMessageBox::question(this,tr("반응 단계 삭제"),tr("선택한 반응 단계를 삭제할까요? 구조는 전체 구조 보관함에 유지됩니다."))!=QMessageBox::Yes)return;reactionStages_.removeAt(activeStage_);activeStage_=reactionStages_.isEmpty()?-2:std::min(activeStage_,int(reactionStages_.size())-1);refreshStageNavigation();refreshStageWorkspace();scheduleDocumentRefresh();});
  auto* pool=new QPushButton(tr("전체 구조 보관함"),panel);pool->setObjectName("showStructurePool");layout->addWidget(pool);connect(pool,&QPushButton::clicked,this,[this]{chooseStageScope(-2);});
  auto* unassigned=new QPushButton(tr("미배정 구조"),panel);unassigned->setObjectName("showUnassignedStructures");layout->addWidget(unassigned);connect(unassigned,&QPushButton::clicked,this,[this]{chooseStageScope(-3);});
  auto* save=new QPushButton(tr("지금 저장 (Ctrl+S)"),panel);layout->addWidget(save);connect(save,&QPushButton::clicked,this,&MainWindow::saveProject);auto* savedNote=new QLabel(tr("전환·종료 시 자동 저장"),panel);savedNote->setWordWrap(true);layout->addWidget(savedNote);
  auto* manage=new QPushButton(tr("프로젝트 관리"),panel);layout->addWidget(manage);connect(manage,&QPushButton::clicked,this,&MainWindow::projectManagerRequested);
  return panel;
}

QWidget* MainWindow::createStructureWorkspace(QWidget* parent){
  auto* box=new QGroupBox(tr("구조와 반응 구성"),parent);auto* layout=new QVBoxLayout(box);layout->setSpacing(8);
  structureScope_=new QLabel(tr("전체 구조 보관함"),box);structureScope_->setWordWrap(true);structureScope_->setStyleSheet("font-weight:600;font-size:12pt;color:#24486c");layout->addWidget(structureScope_);
  stageSettings_=new QWidget(box);auto* stageLayout=new QVBoxLayout(stageSettings_);stageLayout->setContentsMargins(0,0,0,0);
  stageNameEdit_=new QLineEdit(stageSettings_);stageNameEdit_->setObjectName("activeStageName");stageNameEdit_->setPlaceholderText(tr("반응 단계 이름"));stageLayout->addWidget(stageNameEdit_);
  auto* referenceRow=new QHBoxLayout;referenceRow->addWidget(new QLabel(tr("기준 구조"),stageSettings_));stageReference_=new QComboBox(stageSettings_);stageReference_->setObjectName("activeStageReference");referenceRow->addWidget(stageReference_,1);stageLayout->addLayout(referenceRow);
  auto* tables=new QHBoxLayout;const QStringList names={tr("절대 에너지"),tr("상대 에너지"),tr("스핀"),tr("기하")};for(int i=0;i<4;++i){stageTableChecks_[i]=new QCheckBox(names[i],stageSettings_);stageTableChecks_[i]->setObjectName("stageTable_"+tableKeys[i]);tables->addWidget(stageTableChecks_[i]);connect(stageTableChecks_[i],&QCheckBox::toggled,this,[this,i](bool checked){if(updatingStageWorkspace_||activeStage_<0)return;auto stage=reactionStages_[activeStage_].toObject();stage[tableKeys[i]]=checked;reactionStages_[activeStage_]=stage;scheduleDocumentRefresh();});}stageLayout->addLayout(tables);
  auto* figureBox=new QGroupBox(tr("이 반응 Figure · 캡션"),stageSettings_);figureBox->setCheckable(true);auto* figureLayout=new QVBoxLayout(figureBox);stageFigureCaptions_=new QTextEdit(figureBox);stageFigureCaptions_->setObjectName("activeStageFigureCaptions");stageFigureCaptions_->setFixedHeight(54);stageFigureCaptions_->setPlaceholderText(tr("Figure 그룹별 한 줄 · 비우면 반응명과 구조명 사용"));figureLayout->addWidget(stageFigureCaptions_);auto* prepare=new QPushButton(tr("이 반응 Figure 뷰 설정…"),figureBox);figureLayout->addWidget(prepare);connect(prepare,&QPushButton::clicked,this,[this]{prepareFigures();});connect(figureBox,&QGroupBox::toggled,this,[figureBox](bool open){for(auto* child:figureBox->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))child->setVisible(open);});figureBox->setChecked(false);stageLayout->addWidget(figureBox);connect(stageFigureCaptions_,&QTextEdit::textChanged,this,[this]{if(updatingStageWorkspace_||activeStage_<0)return;auto stage=reactionStages_[activeStage_].toObject();stage["figure_captions"]=stageFigureCaptions_->toPlainText();reactionStages_[activeStage_]=stage;scheduleDocumentRefresh();});layout->addWidget(stageSettings_);stageSettings_->hide();
  connect(stageNameEdit_,&QLineEdit::textEdited,this,[this](const QString& text){if(updatingStageWorkspace_||activeStage_<0||text.trimmed().isEmpty())return;auto stage=reactionStages_[activeStage_].toObject();stage["name"]=text.trimmed();reactionStages_[activeStage_]=stage;refreshStageNavigation();scheduleDocumentRefresh();});
  connect(stageReference_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(updatingStageWorkspace_||activeStage_<0)return;auto stage=reactionStages_[activeStage_].toObject();stage["reference"]=stageReference_->currentData().toString();reactionStages_[activeStage_]=stage;scheduleDocumentRefresh();});
  stageGrid_=new StructureAssignmentTable(box,true);stageGrid_->setObjectName("stageWorkspaceGrid");stageGrid_->setMinimumHeight(220);layout->addWidget(stageGrid_,1);
  files_=new QTableWidget(0,7,this);files_->setObjectName("structureList");files_->setHorizontalHeaderLabels({"Structure","Mult.","Freq","SPE","Imag.","Spin","Status"});files_->hide();connect(files_,&QTableWidget::itemSelectionChanged,this,&MainWindow::selectStructure);
  connect(files_,&QTableWidget::itemSelectionChanged,this,[this]{if(updatingStageWorkspace_||files_->currentRow()<0)return;const auto path=files_->item(files_->currentRow(),0)->data(Qt::UserRole).toString();for(int row=0;row<stageGrid_->rowCount();++row)if(stageGrid_->item(row,0)->data(Qt::UserRole).toString()==path){QSignalBlocker blocked(stageGrid_);stageGrid_->setCurrentCell(row,0);if(stageLabelEdit_){QSignalBlocker editBlocked(stageLabelEdit_);stageLabelEdit_->setText(stageGrid_->item(row,0)->text());stageLabelPreview_->setText(si::formattedTextHtml(si::structureLabelMarkup(stageLabelEdit_->text())));}break;}updateStructureOrderButtons();});
  connect(stageGrid_,&QTableWidget::currentCellChanged,this,[this](int row){if(updatingStageWorkspace_||row<0||!stageGrid_->item(row,0))return;const auto path=stageGrid_->item(row,0)->data(Qt::UserRole).toString();for(int i=0;i<files_->rowCount();++i)if(files_->item(i,0)->data(Qt::UserRole).toString()==path){files_->selectRow(i);break;}QSignalBlocker blocked(stageLabelEdit_);stageLabelEdit_->setText(stageGrid_->item(row,0)->text());stageLabelPreview_->setText(si::formattedTextHtml(si::structureLabelMarkup(stageLabelEdit_->text())));});
  connect(stageGrid_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){if(updatingStageWorkspace_||item->column()!=0)return;const auto name=item->text().trimmed();const auto path=item->data(Qt::UserRole).toString();if(name.isEmpty()){refreshStageWorkspace();return;}for(int i=0;i<files_->rowCount();++i)if(files_->item(i,0)->data(Qt::UserRole).toString()==path){const auto old=files_->item(i,0)->text();if(old!=name&&structureLabels().count(old)==1){for(auto* edit:{manualAbsoluteNames_,manualRelativeNames_,spinNames_,geometryNames_}){auto rows=edit->toPlainText().split('\n');for(auto& value:rows)if(value==old)value=name;QSignalBlocker blocked(edit);edit->setPlainText(rows.join('\n'));}}files_->item(i,0)->setText(name);if(files_->currentRow()==i){QSignalBlocker blocked(structureLabel_);structureLabel_->setText(name);}break;}markLabelsEdited();syncReferenceOptions();if(stageGrid_->currentRow()==item->row()&&stageLabelEdit_->text()!=name){QSignalBlocker blocked(stageLabelEdit_);stageLabelEdit_->setText(name);}stageLabelPreview_->setText(si::formattedTextHtml(si::structureLabelMarkup(name)));scheduleDocumentRefresh();});
  auto* actions=new QHBoxLayout;auto* newFile=new QPushButton(tr("새 Output 추가…"),box);actions->addWidget(newFile);connect(newFile,&QPushButton::clicked,this,&MainWindow::chooseFiles);addExistingStage_=new QPushButton(tr("기존 구조에서 추가"),box);addExistingStage_->setObjectName("addExistingStageStructures");actions->addWidget(addExistingStage_);connect(addExistingStage_,&QPushButton::clicked,this,&MainWindow::addExistingStageStructures);
  removeStageMember_=new QPushButton(tr("반응에서 제외"),box);removeStageMember_->setObjectName("removeStageStructure");actions->addWidget(removeStageMember_);connect(removeStageMember_,&QPushButton::clicked,this,&MainWindow::removeStageStructure);layout->addLayout(actions);
  auto* order=new QHBoxLayout;for(int delta:{-1,1}){auto* button=new QPushButton(delta<0?tr("↑ 위로"):tr("↓ 아래로"),box);button->setObjectName(delta<0?"moveStructureUp":"moveStructureDown");order->addWidget(button);connect(button,&QPushButton::clicked,this,[this,delta]{moveStageStructure(delta);});}
  auto* moveTo=new QPushButton(tr("위치 지정"),box);moveTo->setObjectName("moveStructureTo");order->addWidget(moveTo);connect(files_,&QTableWidget::itemSelectionChanged,this,&MainWindow::updateStructureOrderButtons);connect(stageGrid_,&QTableWidget::currentCellChanged,this,[this]{updateStructureOrderButtons();});connect(moveTo,&QPushButton::clicked,this,[this]{if(stageGrid_->currentRow()<0)return;bool accepted=false;int target=QInputDialog::getInt(this,tr("구조 위치"),tr("이동할 위치"),stageGrid_->currentRow()+1,1,stageGrid_->rowCount(),1,&accepted);if(accepted)moveStageStructure(target-1-stageGrid_->currentRow());});layout->addLayout(order);
  auto* nameRow=new QHBoxLayout;stageLabelEdit_=new QLineEdit(box);stageLabelEdit_->setObjectName("workspaceStructureName");stageLabelEdit_->setPlaceholderText(tr("선택 구조 이름 · 프로젝트 전체에 반영"));nameRow->addWidget(stageLabelEdit_,1);
  stageLabelPreview_=new QLabel(box);stageLabelPreview_->setTextFormat(Qt::RichText);nameRow->addWidget(stageLabelPreview_);layout->addLayout(nameRow);connect(stageLabelEdit_,&QLineEdit::textEdited,this,[this](const QString& text){if(!updatingStageWorkspace_&&stageGrid_->currentRow()>=0&&!text.trimmed().isEmpty())stageGrid_->item(stageGrid_->currentRow(),0)->setText(text);});
  auto* formatRow=new QHBoxLayout;for(const auto& marker:QStringList{"^","_"}){auto* button=new QPushButton(marker=="^"?tr("위첨자 x²"):tr("아래첨자 x₂"),box);button->setFocusPolicy(Qt::NoFocus);formatRow->addWidget(button);connect(button,&QPushButton::clicked,this,[this,marker]{const int start=stageLabelEdit_->selectionStart();if(start<0){status_->setText(tr("구조명에서 첨자로 만들 글자를 선택하세요."));return;}const int length=stageLabelEdit_->selectedText().size();auto text=stageLabelEdit_->text();text.replace(start,length,marker+"{"+stageLabelEdit_->selectedText()+"}");stageLabelEdit_->setText(text);if(stageGrid_->currentRow()>=0)stageGrid_->item(stageGrid_->currentRow(),0)->setText(text);stageLabelEdit_->setFocus();stageLabelEdit_->setSelection(start+2,length);});}formatRow->addStretch();auto* details=new QPushButton(tr("추출 정보 보기…"),box);details->setObjectName("showExtractionDetails");formatRow->addWidget(details);connect(details,&QPushButton::clicked,this,&MainWindow::showExtractionDetails);layout->addLayout(formatRow);
  auto* imports=new QGroupBox(tr("파일 추가 · 엑셀 붙여넣기"),box);imports->setObjectName("importControls");imports->setCheckable(true);auto* importLayout=new QVBoxLayout(imports);auto* buttons=new QHBoxLayout;auto* add=new QPushButton(tr("파일 추가…"),imports);auto* folder=new QPushButton(tr("폴더 지정…"),imports);buttons->addWidget(add);buttons->addWidget(folder);importLayout->addLayout(buttons);connect(add,&QPushButton::clicked,this,&MainWindow::chooseFiles);connect(folder,&QPushButton::clicked,this,&MainWindow::chooseDirectory);
  baseDirectory_=new QLineEdit(imports);baseDirectory_->setPlaceholderText(tr("기본 폴더"));importLayout->addWidget(baseDirectory_);fileNames_=new QTextEdit(imports);fileNames_->setObjectName("fileNamesInput");fileNames_->setFixedHeight(80);fileNames_->setLineWrapMode(QTextEdit::NoWrap);fileNames_->setPlaceholderText(tr("구조명 [탭] Output 파일 · 엑셀 두 열 붙여넣기"));importLayout->addWidget(fileNames_);
  auto* addNames=new QPushButton(tr("파일명으로 추가"),imports);addNames->setObjectName("addNamedFiles");connect(addNames,&QPushButton::clicked,this,[this]{addNamedFiles();});importLayout->addWidget(addNames);auto* replace=new QPushButton(tr("전체 구조 목록을 입력 목록으로 교체"),imports);replace->setObjectName("replaceNamedFiles");replace->setToolTip(tr("현재 반응만이 아닌 전체 구조 보관함을 교체합니다."));connect(replace,&QPushButton::clicked,this,[this]{addNamedFiles(true);});importLayout->addWidget(replace);fileNamesInfo_=new QLabel(imports);fileNamesInfo_->setWordWrap(true);importLayout->addWidget(fileNamesInfo_);
  stageGrid_->pasteText=[this](const QString& text){fileNames_->setPlainText(text);addNamedFiles();};
  connect(imports,&QGroupBox::toggled,this,[imports](bool visible){for(auto* child:imports->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))child->setVisible(visible);});layout->addWidget(imports);imports->setChecked(false);
  auto* legacy=new QPushButton(tr("단계 전체 구성…"),box);legacy->setObjectName("reactionStagesButton");connect(legacy,&QPushButton::clicked,this,&MainWindow::editReactionStages);layout->addWidget(legacy);legacy->hide();
  structureLabel_=new QLineEdit(this);structureLabel_->hide();connect(structureLabel_,&QLineEdit::editingFinished,this,&MainWindow::renameSelected);
  return box;
}

void MainWindow::markLabelsEdited(){manualLabelsEdited_=true;labelInputAtManualEdit_=fileNames_->toPlainText();labelBaseAtManualEdit_=baseDirectory_->text();}

void MainWindow::refreshStageNavigation(){
  if(!stageNavigation_)return;QSignalBlocker blocked(stageNavigation_);stageNavigation_->clear();projectHeading_->setText(projectName_.isEmpty()?tr("SI 프로젝트"):projectName_);projectHeading_->setToolTip(currentProjectPath_);
  for(int i=0;i<reactionStages_.size();++i){auto stage=reactionStages_[i].toObject();if(stage["id"].toString().isEmpty()){stage["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);reactionStages_[i]=stage;}auto* item=new QListWidgetItem(stage["name"].toString()+tr("\n구조 %1개").arg(stage["files"].toArray().size()),stageNavigation_);item->setData(Qt::UserRole,stageId(stage));}
  if(activeStage_>=reactionStages_.size())activeStage_=reactionStages_.isEmpty()?-2:0;if(activeStage_>=0)stageNavigation_->setCurrentRow(activeStage_);
}

void MainWindow::chooseStageScope(int stage){activeStage_=stage;if(stage<0){QSignalBlocker blocked(stageNavigation_);stageNavigation_->setCurrentRow(-1);}if(inputPages_&&filesSection_)scrollTo(filesSection_);if(previewScope_){QSignalBlocker blocked(previewScope_);previewScope_->setCurrentIndex(stage<0?1:0);}refreshStageWorkspace();refreshScopedPreview();}

void MainWindow::refreshStageWorkspace(){
  if(!stageGrid_||!files_)return;{QSignalBlocker blocked(stageNavigation_);if(stageNavigation_->currentRow()!=activeStage_)stageNavigation_->setCurrentRow(activeStage_>=0?activeStage_:-1);}updatingStageWorkspace_=true;const bool selected=activeStage_>=0&&activeStage_<reactionStages_.size();const auto stage=selected?reactionStages_[activeStage_].toObject():QJsonObject();const auto paths=sourcePaths(),labels=structureLabels();QStringList visible;
  if(selected){for(const auto& value:stage["files"].toArray())if(paths.contains(value.toString()))visible.append(value.toString());}
  else if(activeStage_==-3){QSet<QString> assigned;for(const auto& value:reactionStages_)for(const auto& file:value.toObject()["files"].toArray())assigned.insert(file.toString());for(const auto& path:paths)if(!assigned.contains(path))visible.append(path);}
  else visible=paths;
  const auto masterPath=files_->currentRow()>=0?files_->item(files_->currentRow(),0)->data(Qt::UserRole).toString():QString();const auto selectedPath=visible.contains(masterPath)?masterPath:stageGrid_->currentRow()>=0&&stageGrid_->item(stageGrid_->currentRow(),0)?stageGrid_->item(stageGrid_->currentRow(),0)->data(Qt::UserRole).toString():QString();bool rebuild=stageGrid_->rowCount()!=visible.size();if(!rebuild)for(int i=0;i<visible.size();++i)if(stageGrid_->item(i,0)->data(Qt::UserRole).toString()!=visible[i]||stageGrid_->item(i,0)->text()!=labels.value(paths.indexOf(visible[i])))rebuild=true;
  if(rebuild){QSignalBlocker blocked(stageGrid_);stageGrid_->setRowCount(0);stageGrid_->setRowCount(visible.size());for(int i=0;i<visible.size();++i){auto* label=new QTableWidgetItem(labels.value(paths.indexOf(visible[i])));label->setData(Qt::UserRole,visible[i]);stageGrid_->setItem(i,0,label);auto* output=new QTableWidgetItem(QFileInfo(visible[i]).fileName());output->setFlags(output->flags()&~Qt::ItemIsEditable);output->setToolTip(visible[i]);stageGrid_->setItem(i,1,output);}if(!visible.isEmpty())stageGrid_->setCurrentCell(std::max(0,int(visible.indexOf(selectedPath))),0);}
  structureScope_->setText(selected?stage["name"].toString()+tr(" · 구조 %1개").arg(visible.size()):activeStage_==-3?tr("미배정 구조 · %1개").arg(visible.size()):activeStage_==-1?tr("전체 SI · 구조 %1개").arg(visible.size()):tr("전체 구조 보관함 · %1개").arg(visible.size()));stageSettings_->setVisible(selected);addExistingStage_->setEnabled(selected&&!parsing_);removeStageMember_->setText(selected?tr("이 반응에서 제외"):tr("보관함에서 제거"));
  {QSignalBlocker blocked(stageNameEdit_);if(stageNameEdit_->text()!=stage["name"].toString())stageNameEdit_->setText(stage["name"].toString());} {QSignalBlocker blocked(stageReference_);stageReference_->clear();for(const auto& path:visible)stageReference_->addItem(labels.value(paths.indexOf(path)),path);stageReference_->setCurrentIndex(std::max(0,stageReference_->findData(stage["reference"].toString())));}
  {QSignalBlocker blocked(stageFigureCaptions_);if(stageFigureCaptions_->toPlainText()!=stage["figure_captions"].toString())stageFigureCaptions_->setPlainText(stage["figure_captions"].toString());}captions_->setEnabled(reactionStages_.isEmpty());captions_->setToolTip(tr("반응별 캡션은 구조 화면의 Figure · 캡션에서 입력합니다."));
  for(int i=0;i<4;++i){QSignalBlocker blocked(stageTableChecks_[i]);stageTableChecks_[i]->setChecked(stage.value(tableKeys[i]).toBool(i==0?includeAbsolute_->isChecked():true));}
  const int row=stageGrid_->currentRow();const auto text=row>=0?stageGrid_->item(row,0)->text():QString();{QSignalBlocker blocked(stageLabelEdit_);if(stageLabelEdit_->text()!=text)stageLabelEdit_->setText(text);}stageLabelEdit_->setEnabled(row>=0);stageLabelPreview_->setText(si::formattedTextHtml(si::structureLabelMarkup(text)));updatingStageWorkspace_=false;updateStructureStatusCells();updateStructureOrderButtons();
  if(rebuild&&row>=0){const auto path=stageGrid_->item(row,0)->data(Qt::UserRole).toString();const int master=paths.indexOf(path);if(master>=0&&master!=files_->currentRow())files_->selectRow(master);}
}

void MainWindow::updateStructureStatusCells(){
  if(!stageGrid_||!files_||stageGrid_->columnCount()<7)return;
  QHash<QString,int> sourceRows;
  for(int row=0;row<files_->rowCount();++row)
    if(const auto* label=files_->item(row,0))sourceRows.insert(label->data(Qt::UserRole).toString(),row);
  QSignalBlocker blocked(stageGrid_);
  constexpr int sourceColumns[]={1,2,3,4,6};
  for(int row=0;row<stageGrid_->rowCount();++row){
    const auto* label=stageGrid_->item(row,0);
    if(!label)continue;
    const int sourceRow=sourceRows.value(label->data(Qt::UserRole).toString(),-1);
    if(sourceRow<0)continue;
    for(int index=0;index<5;++index){
      const auto* source=files_->item(sourceRow,sourceColumns[index]);
      const int targetColumn=index+2;
      if(!stageGrid_->item(row,targetColumn)){
        auto* item=new QTableWidgetItem;
        item->setFlags(item->flags()&~Qt::ItemIsEditable);
        stageGrid_->setItem(row,targetColumn,item);
      }
      auto* target=stageGrid_->item(row,targetColumn);
      QString value=source?source->text():QString();
      if(targetColumn==6){
        if(value=="Ready")value=tr("✓ 정상");
        else if(value=="Warning")value=tr("! 주의");
        else if(value=="Error")value=tr("✕ 오류");
        else if(value.isEmpty())value=tr("대기 중");
        const QColor color=value.contains(tr("오류"))?QColor("#b42318"):
                           value.contains(tr("주의"))?QColor("#9a6700"):
                           value.contains(tr("대기"))?QColor("#637083"):QColor("#245b43");
        target->setForeground(QBrush(color));
      }
      if(target->text()!=value)target->setText(value);
      target->setToolTip(source?source->toolTip():QString());
      target->setTextAlignment(Qt::AlignCenter);
    }
  }
}

void MainWindow::refreshScopedPreview(){
  if(!preview_)return;auto model=documentModel_;if(activeStage_>=0&&activeStage_<reactionStages_.size()&&previewScope_&&previewScope_->currentData().toString()=="current"){
    const auto stage=reactionStages_[activeStage_].toObject();const auto id=stageId(stage);model.tables.erase(std::remove_if(model.tables.begin(),model.tables.end(),[&](const auto& table){return table.stageId!=id;}),model.tables.end());
    QVector<si::CoordinateBlock> coordinates;const auto paths=sourcePaths();for(const auto& file:stage["files"].toArray()){const int index=paths.indexOf(file.toString());if(index>=0&&index<model.coordinates.size())coordinates.append(model.coordinates[index]);}model.coordinates=coordinates;QVector<si::DocumentFigure> figures;QStringList captions;for(int i=0;i<model.figures.size();++i)if(model.figures[i].stageId==id){figures.append(model.figures[i]);captions.append(model.figureCaptions.value(i));}model.figures=figures;model.figureCaptions=captions;model.stageOrder={id};model.computationalDetails.clear();model.referencesText.clear();model.mechanismName=stage["name"].toString();
  }preview_->setDocumentModel(model);
}

void MainWindow::addReactionStage(bool duplicate){
  if(duplicate&&(activeStage_<0||activeStage_>=reactionStages_.size()))return;bool accepted=false;auto stage=duplicate?reactionStages_[activeStage_].toObject():QJsonObject();const auto suggestion=duplicate?stage["name"].toString()+tr(" 복사"):tr("반응 단계 %1").arg(reactionStages_.size()+1);const auto name=QInputDialog::getText(this,tr("반응 단계"),tr("이름"),QLineEdit::Normal,suggestion,&accepted).trimmed();if(!accepted||name.isEmpty())return;stage["name"]=name;stage["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);if(!duplicate){stage["files"]=QJsonArray();stage["absolute"]=false;}reactionStages_.append(stage);activeStage_=reactionStages_.size()-1;refreshStageNavigation();chooseStageScope(activeStage_);scheduleDocumentRefresh();
}

void MainWindow::addExistingStageStructures(){
  if(activeStage_<0||parsing_)return;QDialog dialog(this);dialog.setWindowTitle(tr("이 반응에 구조 추가"));dialog.resize(700,450);auto* layout=new QVBoxLayout(&dialog);auto* table=new StructureAssignmentTable(&dialog);layout->addWidget(table);const auto paths=sourcePaths(),labels=structureLabels();auto stage=reactionStages_[activeStage_].toObject();auto members=stage["files"].toArray();for(int i=0;i<paths.size();++i)if(!members.contains(paths[i])){const int row=table->rowCount();table->insertRow(row);auto* item=new QTableWidgetItem(labels[i]);item->setData(Qt::UserRole,paths[i]);item->setFlags((item->flags()&~Qt::ItemIsEditable)|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Unchecked);table->setItem(row,0,item);auto* file=new QTableWidgetItem(QFileInfo(paths[i]).fileName());file->setFlags(file->flags()&~Qt::ItemIsEditable);table->setItem(row,1,file);}auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;for(int i=0;i<table->rowCount();++i)if(table->item(i,0)->checkState()==Qt::Checked)members.append(table->item(i,0)->data(Qt::UserRole).toString());stage["files"]=members;if(stage["reference"].toString().isEmpty()&&!members.isEmpty())stage["reference"]=members.first();reactionStages_[activeStage_]=stage;refreshStageNavigation();refreshStageWorkspace();scheduleDocumentRefresh();
}

void MainWindow::removeStageStructure(){
  const int row=stageGrid_->currentRow();if(row<0)return;if(activeStage_<0){removeSelected();refreshStageWorkspace();return;}auto stage=reactionStages_[activeStage_].toObject();auto members=stage["files"].toArray();const auto path=stageGrid_->item(row,0)->data(Qt::UserRole).toString();for(int i=members.size()-1;i>=0;--i)if(members[i].toString()==path)members.removeAt(i);stage["files"]=members;if(stage["reference"].toString()==path)stage["reference"]=members.isEmpty()?QString():members.first().toString();reactionStages_[activeStage_]=stage;refreshStageNavigation();refreshStageWorkspace();scheduleDocumentRefresh();
}

void MainWindow::moveStageStructure(int delta){
  const int row=stageGrid_->currentRow(),target=row+delta;if(parsing_||row<0||target<0||target>=stageGrid_->rowCount())return;if(activeStage_<0){moveSelectedStructure(sourcePaths().indexOf(stageGrid_->item(target,0)->data(Qt::UserRole).toString()));refreshStageWorkspace();return;}auto stage=reactionStages_[activeStage_].toObject();auto members=stage["files"].toArray();const auto path=stageGrid_->item(row,0)->data(Qt::UserRole).toString();int from=-1;for(int i=0;i<members.size();++i)if(members[i].toString()==path)from=i;if(from<0)return;const auto value=members.takeAt(from);members.insert(target,value);stage["files"]=members;reactionStages_[activeStage_]=stage;refreshStageWorkspace();scheduleDocumentRefresh();
}

void MainWindow::showExtractionDetails(){
  refreshPreflight();QDialog dialog(this);dialog.setObjectName("extractionDetailsDialog");dialog.setWindowTitle(tr("추출 정보 · 선택 확인"));dialog.resize(950,480);auto* layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel(tr("원본 파일에서 추출한 정보를 필요할 때 확인하는 화면입니다. 셀에 마우스를 올리면 상세 내용을 볼 수 있습니다."),&dialog));auto* table=new QTableWidget(files_->rowCount(),7,&dialog);QStringList headers;for(int col=0;col<7;++col)headers.append(files_->horizontalHeaderItem(col)->text());table->setHorizontalHeaderLabels(headers);for(int row=0;row<files_->rowCount();++row)for(int col=0;col<7;++col)if(auto* item=files_->item(row,col))table->setItem(row,col,item->clone());table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(table);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
}

void MainWindow::updateStructureOrderButtons(){
  if(!stageGrid_||!files_)return;const int row=activeStage_>=0||activeStage_==-3?stageGrid_->currentRow():files_->currentRow();const int count=stageGrid_->rowCount();
  if(auto* button=findChild<QPushButton*>("moveStructureUp"))button->setEnabled(!parsing_&&row>0);
  if(auto* button=findChild<QPushButton*>("moveStructureDown"))button->setEnabled(!parsing_&&row>=0&&row<count-1);
  if(auto* button=findChild<QPushButton*>("moveStructureTo"))button->setEnabled(!parsing_&&row>=0&&count>1);
}
