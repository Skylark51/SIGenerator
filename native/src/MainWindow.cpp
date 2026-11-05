#include "MainWindow.hpp"
#include "WindowGeometry.hpp"
#include <QShowEvent>
#include "DocumentPreviewWidget.hpp"
#include "MoleculeViewWidget.hpp"
#include "StructureAssignmentTable.hpp"
#include "si/SIModel.hpp"
#include "si/StructureLabels.hpp"

#include <QListWidget>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QToolBar>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QCheckBox>
#include <QFile>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
// Draw at device resolution so toolbar icons stay crisp at every display scale.
QIcon navigationIcon(const QString& kind,const QColor& color,int badge=0){
  QIcon icon;
  for(int scale:{1,2,3}){
    QPixmap pixmap(24*scale,24*scale);pixmap.setDevicePixelRatio(scale);pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(color,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(kind=="structures"||kind=="geometry"){
      const QPointF a(5,17),b(12,5),c(20,16);p.drawLine(a,b);p.drawLine(b,c);if(kind=="structures")p.drawLine(a,c);
      p.setBrush(QColor("#ffffff"));for(auto point:{a,b,c})p.drawEllipse(point,2.6,2.6);
    }else if(kind=="energy"){
      p.drawLine(3,20,21,20);p.drawLine(3,20,3,4);p.drawLine(6,16,10,16);p.drawLine(10,16,14,8);p.drawLine(14,8,20,8);
    }else if(kind=="spin"){
      p.drawEllipse(QRectF(3,7,18,10));p.drawEllipse(QRectF(7,3,10,18));p.setBrush(color);p.drawEllipse(QPointF(12,12),2,2);
    }else if(kind=="indices"){
      p.drawText(QRectF(2,2,20,20),Qt::AlignCenter,QStringLiteral("1 2"));p.drawLine(4,21,20,21);
    }else if(kind=="reset"){
      p.drawArc(QRectF(5,5,15,15),30*16,285*16);p.drawLine(4,4,4,10);p.drawLine(4,10,10,10);
    }else if(kind=="figure"){
      p.drawRoundedRect(QRectF(3,3,18,18),2,2);p.drawEllipse(QPointF(8,8),1.6,1.6);p.drawLine(4,18,10,12);p.drawLine(10,12,14,16);p.drawLine(14,16,18,11);p.drawLine(18,11,21,15);
    }else{
      p.drawRoundedRect(QRectF(5,2,14,20),2,2);
      if(kind=="document"){for(int y:{8,12,16})p.drawLine(8,y,16,y);}
      else{QFont font=p.font();font.setPixelSize(kind=="xyz"?7:9);font.setBold(true);p.setFont(font);p.drawText(QRectF(5,4,14,12),Qt::AlignCenter,kind=="si"?"SI":kind=="xyz"?"XYZ":"x,y");p.drawLine(9,17,15,17);}
    }
    if(badge>0){
      p.setPen(Qt::NoPen);p.setBrush(QColor("#244e7c"));p.drawRoundedRect(QRectF(0,0,11,12),3,3);
      QFont font=p.font();font.setPixelSize(10);font.setBold(true);p.setFont(font);p.setPen(Qt::white);
      p.drawText(QRectF(0,0,11,12),Qt::AlignCenter,QString::number(badge));
    }
    p.end();icon.addPixmap(pixmap);
  }
  return icon;
}
QSettings windowSettings(){return QSettings(QStringLiteral("SI Generator"),qEnvironmentVariableIsSet("SI_GENERATOR_TEST_SETTINGS")?QStringLiteral("SIGeneratorTests"):QStringLiteral("SIGenerator"));}
QGroupBox* section(const QString& title,QWidget* parent,bool collapsible=false){auto* box=new QGroupBox(QString(title).replace("&","&&"),parent);if(collapsible){box->setCheckable(true);box->setChecked(false);QObject::connect(box,&QGroupBox::toggled,box,[box](bool open){for(auto* child:box->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))child->setVisible(open);});}return box;}
}

MainWindow::MainWindow(){
  setObjectName(QStringLiteral("mainWindow"));setAcceptDrops(true);setWindowTitle(tr("SI Generator — 네이티브 (Native)"));resize(1480,900);
  auto* fileMenu=menuBar()->addMenu(tr("파일 (File)"));fileMenu->addAction(tr("Gaussian 파일 열기 (Open Files)…"),QKeySequence::Open,this,&MainWindow::chooseFiles);fileMenu->addAction(tr("고급 · 단계 일괄 구성…"),this,&MainWindow::editReactionStages);fileMenu->addAction(tr("프로젝트 관리…"),this,&MainWindow::projectManagerRequested);fileMenu->addAction(tr("프로젝트 열기 (Open Project)…"),this,&MainWindow::openProject);fileMenu->addAction(tr("프로젝트 저장 (Save Project)…"),QKeySequence::Save,this,&MainWindow::saveProject);
  fileMenu->addAction(tr("프로젝트 버전 이력…"),this,&MainWindow::showProjectHistory)->setObjectName("projectHistoryAction");
  auto* splitter=new QSplitter(Qt::Horizontal,this);mainSplitter_=splitter;splitter->setObjectName(QStringLiteral("mainSplitter"));splitter->addWidget(createStageNavigation());splitter->addWidget(createInputPanel());splitter->addWidget(createWorkspace());splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setStretchFactor(2,1);splitter->setSizes({210,635,635});
  connect(splitter,&QSplitter::splitterMoved,this,[this]{
    if(workspaceFocused_||restoringSplitterState_||!splitterStateRestored_)return;
    const auto sizes=mainSplitter_->sizes();
    if(sizes.size()!=3||std::any_of(sizes.begin(),sizes.end(),[](int value){return value<=0;}))return;
    windowSettings().setValue(QStringLiteral("mainSplitterState"),mainSplitter_->saveState());
  });
  auto* workspaceScroll=new QScrollArea(this);workspaceScroll->setObjectName("workspaceScroll");workspaceScroll->setWidgetResizable(true);workspaceScroll->setFrameShape(QFrame::NoFrame);workspaceScroll->setWidget(splitter);setCentralWidget(workspaceScroll);createQuickToolbar();
  previewTimer_=new QTimer(this);previewTimer_->setSingleShot(true);previewTimer_->setInterval(220);connect(previewTimer_,&QTimer::timeout,this,&MainWindow::refreshDocumentModel);connectPreviewInputs();
  auto settings=windowSettings();baseDirectory_->setText(settings.value(QStringLiteral("baseDirectory")).toString());restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
  {QSignalBlocker a(referencesText_),b(computationalDetails_);referencesText_->setPlainText(settings.value("documentDraft/referencesText").toString());
   detailsEdited_=settings.value("documentDraft/detailsEdited",false).toBool();if(detailsEdited_)computationalDetails_->setPlainText(settings.value("documentDraft/computationalDetails").toString());}
  connect(referencesText_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();const auto value=referencesText_->toPlainText();draft.setValue("documentDraft/referencesText",value);if(!value.trimmed().isEmpty())draft.setValue("documentDraft/lastNonemptyReferences",value);});
  connect(computationalDetails_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();draft.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());draft.setValue("documentDraft/detailsEdited",detailsEdited_);});
  scheduleDocumentRefresh();
}

void MainWindow::configureStructureColumns(QToolButton* button){
  constexpr int defaultColumns=(1<<1)|(1<<4)|(1<<6);
  const int visible=windowSettings().value("structureList/visibleColumns",defaultColumns).toInt();
  auto* menu=new QMenu(button);
  for(int column=1;column<stageGrid_->columnCount();++column){
    auto* action=menu->addAction(stageGrid_->horizontalHeaderItem(column)->text());
    action->setCheckable(true);
    action->setChecked(visible&(1<<column));
    stageGrid_->setColumnHidden(column,!action->isChecked());
    connect(action,&QAction::toggled,this,[this,column](bool checked){
      stageGrid_->setColumnHidden(column,!checked);
      int mask=0;
      for(int i=1;i<stageGrid_->columnCount();++i)if(!stageGrid_->isColumnHidden(i))mask|=1<<i;
      windowSettings().setValue("structureList/visibleColumns",mask);
    });
  }
  button->setMenu(menu);
  button->setPopupMode(QToolButton::InstantPopup);
}
MainWindow::~MainWindow(){if(importWatcher_)importWatcher_->cancel();auto settings=windowSettings();settings.setValue(QStringLiteral("baseDirectory"),baseDirectory_->text());settings.setValue(QStringLiteral("geometry"),saveGeometry());settings.setValue("documentDraft/referencesText",referencesText_->toPlainText());settings.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());settings.setValue("documentDraft/detailsEdited",detailsEdited_);}

void MainWindow::showEvent(QShowEvent* event){
  QMainWindow::showEvent(event);
  QTimer::singleShot(0,this,[this]{
    fitWindowToScreen(this);
    if(splitterStateRestored_||!mainSplitter_)return;
    splitterStateRestored_=true;
    const auto bytes=windowSettings().value(QStringLiteral("mainSplitterState")).toByteArray();
    restoringSplitterState_=true;
    if(bytes.isEmpty()||!mainSplitter_->restoreState(bytes)||
       mainSplitter_->sizes().size()!=3||
       ([](const QList<int>& sizes){return std::any_of(sizes.cbegin(),sizes.cend(),[](int value){return value<=0;});})(mainSplitter_->sizes()))
      mainSplitter_->setSizes({210,635,635});
    restoringSplitterState_=false;
  });
}

QWidget* MainWindow::createInputPanel(){
  inputPages_=new QStackedWidget(this);inputPages_->setObjectName("inputPanel");inputPages_->setMinimumWidth(380);auto* content=new QWidget(this);auto* layout=new QVBoxLayout(content);layout->setContentsMargins(10,10,10,10);layout->setSpacing(8);
  auto* heading=new QLabel(tr("입력 (Input)"),content);QFont headingFont=heading->font();headingFont.setPointSize(16);headingFont.setBold(true);heading->setFont(headingFont);layout->addWidget(heading);
  auto* filesBox=createStructureWorkspace(content);filesSection_=filesBox;layout->addWidget(filesBox);
  energySection_=section(tr("에너지와 기준점 (Energy & Reference)"),content);auto* energy=new QFormLayout(energySection_);referenceMode_=new QComboBox(energySection_);referenceMode_->addItem(tr("단일 구조 (Single Structure)"),QStringLiteral("single"));referenceMode_->addItem(tr("수동 기준값 (Manual Reference)"),QStringLiteral("manual_numeric"));referenceMode_->addItem(tr("수동 상대값만 (Manual Relative Only)"),QStringLiteral("manual_relative_only"));energy->addRow(tr("기준 방식 (Reference Mode)"),referenceMode_);reference_=new QComboBox(energySection_);reference_->setObjectName("referenceStructure");energy->addRow(tr("기준 구조 (Reference Structure)"),reference_);manualReference_=new QLineEdit(energySection_);manualReference_->setPlaceholderText(tr("Hartree"));energy->addRow(tr("수동 기준값 (Manual Reference)"),manualReference_);energyBasis_=new QComboBox(energySection_);energyBasis_->addItem(tr("기존 Python 방식 (성분별 SI 에너지)"),static_cast<int>(si::EnergyBasis::LegacyComposite));energyBasis_->addItem(tr("Composite Gibbs: E_SPE + (G_freq - E_freq)"),static_cast<int>(si::EnergyBasis::DefaultComposite));energyBasis_->addItem(tr("전자 에너지 (Electronic)"),static_cast<int>(si::EnergyBasis::Electronic));energyBasis_->addItem(tr("전자 + ZPE (Electronic + ZPE)"),static_cast<int>(si::EnergyBasis::ElectronicZpe));energyBasis_->addItem(tr("엔탈피 (Enthalpy)"),static_cast<int>(si::EnergyBasis::Enthalpy));energyBasis_->addItem(tr("Gibbs 자유에너지 (Gibbs)"),static_cast<int>(si::EnergyBasis::Gibbs));energy->addRow(tr("에너지 기준 (Energy Basis)"),energyBasis_);energyInputMode_=new QComboBox(energySection_);energyInputMode_->addItem(tr("자동 추출 (Automatic)"),QStringLiteral("automatic"));energyInputMode_->addItem(tr("수동 입력 (Manual)"),QStringLiteral("manual"));energy->addRow(tr("입력 방식 (Input Mode)"),energyInputMode_);includeAbsolute_=new QCheckBox(tr("절대 에너지 표 포함 (Include Absolute Energy)"),energySection_);includeAbsolute_->setChecked(false);energy->addRow(includeAbsolute_);includeDispersion_=new QCheckBox(tr("상대 에너지에 분산 보정 포함 (Include Dispersion)"),energySection_);includeDispersion_->setChecked(false);energy->addRow(includeDispersion_);includeComplexation_=new QCheckBox(tr("상대 에너지에 착물화 보정 포함 (Include Complexation)"),energySection_);includeComplexation_->setChecked(false);energy->addRow(includeComplexation_);manualAbsoluteNames_=new QTextEdit(energySection_);manualAbsoluteNames_->setPlaceholderText(tr("절대 에너지 구조명 — 한 줄에 하나 (Absolute Names)"));manualAbsoluteNames_->setFixedHeight(48);energy->addRow(manualAbsoluteNames_);manualAbsoluteValues_=new QTextEdit(energySection_);manualAbsoluteValues_->setPlaceholderText(tr("절대 에너지 값 — 공백/탭 구분 (Absolute Values)"));manualAbsoluteValues_->setFixedHeight(48);energy->addRow(manualAbsoluteValues_);manualRelativeNames_=new QTextEdit(energySection_);manualRelativeNames_->setPlaceholderText(tr("상대 에너지 구조명 (Relative Names)"));manualRelativeNames_->setFixedHeight(48);energy->addRow(manualRelativeNames_);manualRelativeValues_=new QTextEdit(energySection_);manualRelativeValues_->setPlaceholderText(tr("상대 에너지 값 (Relative Values)"));manualRelativeValues_->setFixedHeight(48);energy->addRow(manualRelativeValues_);
  auto* relativeOrder=new QLabel(energySection_);relativeOrder->setWordWrap(true);energy->addRow(relativeOrder);
  auto updateRelativeOrder=[this,relativeOrder]{
    QStringList columns{"ΔE","ΔΔEᵃ","ETotal","ΔZ₀","ΔΔEThermalᵇ","−TΔSᵇ"};
    if(includeDispersion_->isChecked())columns.append("ΔDispersion");
    if(includeComplexation_->isChecked())columns.append("ΔComplexation");
    columns.append("ΔGᶜ");
    const auto hint=tr("수동 상대값 (kcal/mol): 한 구조당 %1개 값을 공백/탭으로 구분\n%2\n보정 옵션을 먼저 선택하세요. ΔG도 직접 입력하며 재계산하지 않습니다. 기존 9열 입력도 지원합니다.").arg(columns.size()).arg(columns.join(" | "));
    relativeOrder->setText(hint);manualRelativeValues_->setToolTip(hint);manualRelativeValues_->setPlaceholderText(columns.join("  "));
  };
  connect(includeDispersion_,&QCheckBox::toggled,this,updateRelativeOrder);
  connect(includeComplexation_,&QCheckBox::toggled,this,updateRelativeOrder);
  updateRelativeOrder();layout->addWidget(energySection_);
  spinSection_=section(tr("Mulliken 스핀 밀도 (Spin Density)"),content,true);auto* spin=new QFormLayout(spinSection_);spinNames_=new QTextEdit(spinSection_);spinNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));spinNames_->setFixedHeight(45);spin->addRow(spinNames_);spinIndices_=new QTextEdit(spinSection_);spinIndices_->setPlaceholderText(tr("원자 ID — 비우면 절댓값 상위 6개 자동 선택 (Atom IDs)"));spinIndices_->setFixedHeight(45);spin->addRow(spinIndices_);spinAtomNumbers_=new QCheckBox(tr("원자 번호 표시 (Show Atom Numbers)"),spinSection_);spinAtomNumbers_->setObjectName("spinAtomNumbers");spinAtomNumbers_->setChecked(true);spin->addRow(spinAtomNumbers_);layout->addWidget(spinSection_);
  geometrySection_=section(tr("선택 구조 (Selected Geometry)"),content,true);auto* geometry=new QFormLayout(geometrySection_);geometryNames_=new QTextEdit(geometrySection_);geometryNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));geometryNames_->setFixedHeight(45);geometry->addRow(geometryNames_);geometryIndices_=new QTextEdit(geometrySection_);geometryIndices_->setPlaceholderText(tr("2/3/4개 원자 ID: 거리/각/이면각 — 비우면 RMSD 자동 분석"));geometryIndices_->setFixedHeight(55);geometry->addRow(geometryIndices_);geometryAtomNumbers_=new QCheckBox(tr("원자 번호 표시 (Show Atom Numbers)"),geometrySection_);geometryAtomNumbers_->setObjectName("geometryAtomNumbers");geometryAtomNumbers_->setChecked(true);geometry->addRow(geometryAtomNumbers_);geometryAutoInfo_=new QLabel(geometrySection_);geometryAutoInfo_->setWordWrap(true);geometryAutoInfo_->setObjectName("geometryAutoInfo");geometry->addRow(geometryAutoInfo_);auto* addGeometry=new QPushButton(tr("클릭 선택 추가 (Add Click Selection)"),geometrySection_);connect(addGeometry,&QPushButton::clicked,this,&MainWindow::appendSelectedGeometry);geometry->addRow(addGeometry);layout->addWidget(geometrySection_);
  auto* documentBox=section(tr("문서와 출력 (Document & Output)"),content);documentSection_=documentBox;auto* document=new QFormLayout(documentBox);documentTitle_=new QLineEdit(QStringLiteral("Supporting Information"),documentBox);document->addRow(tr("문서 제목 (Document Title)"),documentTitle_);mechanismName_=new QLineEdit(documentBox);mechanismName_->setPlaceholderText(tr("반응/메커니즘 이름 (Mechanism Name)"));document->addRow(tr("메커니즘 (Mechanism)"),mechanismName_);decimalPlaces_=new QSpinBox(documentBox);decimalPlaces_->setRange(0,10);decimalPlaces_->hide(); /* Retain legacy project-field compatibility; precision is defined per quantity. */ captions_=new QTextEdit(documentBox);captions_->setPlaceholderText(tr("그림 캡션 — Figure 그룹당 한 줄 (첫 3개, 이후 2개)"));captions_->setFixedHeight(60);document->addRow(captions_);siOutputPath_=new QLineEdit(documentBox);siOutputPath_->setPlaceholderText(tr("SI DOCX 출력 경로 (SI Output Path)"));document->addRow(siOutputPath_);xyzOutputPath_=new QLineEdit(documentBox);xyzOutputPath_->setPlaceholderText(tr("XYZ 출력 경로 (*.xyz)"));document->addRow(xyzOutputPath_);manuscriptTitle_=new QLineEdit(documentBox);manuscriptTitle_->setObjectName("manuscriptTitle");document->addRow("Manuscript title",manuscriptTitle_);
  auto* prepare=new QPushButton(tr("분자 Figure 뷰 설정 (첫 3개 · 이후 2개)"),documentBox);prepare->setObjectName("prepareFiguresButton");document->addRow(prepare);
  connect(prepare,&QPushButton::clicked,this,[this]{prepareFigures();});
  authors_=new QLineEdit(documentBox);document->addRow("Authors",authors_);
  affiliations_=new QLineEdit(documentBox);document->addRow("Affiliations",affiliations_);
  correspondingEmail_=new QLineEdit(documentBox);document->addRow("Corresponding email",correspondingEmail_);
  computationalDetails_=new QTextEdit(documentBox);computationalDetails_->setObjectName("computationalDetails");computationalDetails_->setMinimumHeight(120);document->addRow("Computational Details",computationalDetails_);
  connect(computationalDetails_,&QTextEdit::textChanged,this,[this]{detailsEdited_=true;});
  auto* regenerate=new QPushButton("Regenerate computational details",documentBox);document->addRow(regenerate);
  connect(regenerate,&QPushButton::clicked,this,[this]{detailsEdited_=false;scheduleDocumentRefresh();});
  referencesText_=new QTextEdit(documentBox);referencesText_->setFixedHeight(60);referencesText_->setObjectName("referencesText");document->addRow("References",referencesText_);
  auto* publishedRefs=new QPushButton(tr("출판 논문 계산 문헌 S24–S29 선택·추가"),documentBox);publishedRefs->setObjectName("publishedReferenceLibrary");document->addRow(publishedRefs);
  connect(publishedRefs,&QPushButton::clicked,this,[this]{
    QFile file(":/si/published_computational_references.json");
    if(!file.open(QIODevice::ReadOnly)){status_->setText(tr("내장 계산 문헌을 읽을 수 없습니다."));return;}
    const auto library=QJsonDocument::fromJson(file.readAll()).object();
    QDialog dialog(this);dialog.setObjectName("publishedReferenceDialog");dialog.setWindowTitle(tr("출판 논문 계산 문헌 S24–S29"));dialog.resize(820,650);
    auto* layout=new QVBoxLayout(&dialog);
    auto* note=new QLabel(tr("출처: 출판 논문 SI.pdf, pp. 7–8 / 80–81. 원문의 S 번호와 서지정보를 유지합니다.\nS25: Gaussian 16 Rev. B.01 · S27(b): Coulomb-fitting basis · S29: 분산 감쇠 함수.\n실제 계산 조건에 맞는 문헌을 선택하세요. 추가 후 본문의 인용과 번호를 맞춰 주세요."),&dialog);note->setWordWrap(true);layout->addWidget(note);
    QVector<QCheckBox*> choices;QStringList citations;
    for(const auto& value:library.value("entries").toArray()){
      const auto entry=value.toObject();auto* choice=new QCheckBox(entry.value("label").toString()+" — "+entry.value("topic").toString(),&dialog);
      choice->setObjectName(entry.value("label").toString());choice->setChecked(true);layout->addWidget(choice);choices.append(choice);citations.append(entry.value("text").toString());
    }
    auto* preview=new QTextEdit(&dialog);preview->setReadOnly(true);preview->setObjectName("publishedReferencePreview");layout->addWidget(preview,1);
    auto update=[choices,citations,preview]{QStringList selected;for(int i=0;i<choices.size();++i)if(choices[i]->isChecked())selected.append(citations[i]);preview->setPlainText(selected.join("\n\n"));};
    for(auto* choice:choices)connect(choice,&QCheckBox::toggled,&dialog,update);update();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(tr("선택 문헌 추가"));layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    QString current=referencesText_->toPlainText();int added=0;
    for(int i=0;i<choices.size();++i)if(choices[i]->isChecked()&&!current.contains(citations[i])){if(!current.trimmed().isEmpty())current+="\n\n";current+=citations[i];++added;}
    referencesText_->setPlainText(current);status_->setText(tr("출판 논문 계산 문헌 %1개 추가됨 (동일 원문 중복 제외).").arg(added));
  });
  auto* restoreRefs=new QPushButton(tr("이전 References 입력 복원"),documentBox);restoreRefs->setObjectName("restoreReferenceDraft");document->addRow(restoreRefs);
  connect(restoreRefs,&QPushButton::clicked,this,[this]{auto draft=windowSettings();const auto value=draft.value("documentDraft/lastNonemptyReferences").toString();if(!value.isEmpty())referencesText_->setPlainText(value);else status_->setText(tr("저장된 이전 References 입력이 없습니다."));});
  auto* originalRefs=new QPushButton(tr("원본 SI의 계산 문헌 S5–S7 불러오기"),documentBox);originalRefs->setObjectName("restoreOriginalReferences");document->addRow(originalRefs);
  connect(originalRefs,&QPushButton::clicked,this,[this]{QFile file(":/si/recovered_computational_references.txt");if(file.open(QIODevice::ReadOnly)){referencesText_->setPlainText(QString::fromUtf8(file.readAll()));status_->setText(tr("SI_261027_revised.docx의 S5–S7을 불러왔습니다. 원문은 Gaussian 09/6-31G* 기준이므로 현재 계산의 버전·basis 문헌은 확인하세요."));}});
  auto* projects=new QHBoxLayout;auto* save=new QPushButton(tr("저장 (Save Project)"),documentBox);auto* open=new QPushButton(tr("불러오기 (Load Project)"),documentBox);connect(save,&QPushButton::clicked,this,&MainWindow::saveProject);connect(open,&QPushButton::clicked,this,&MainWindow::openProject);projects->addWidget(save);projects->addWidget(open);document->addRow(projects);layout->addWidget(documentBox);
  for(auto* group:{filesSection_,energySection_,spinSection_,geometrySection_,documentSection_}){
    layout->removeWidget(group);
    if(auto* form=qobject_cast<QFormLayout*>(group->layout())){
      form->setRowWrapPolicy(QFormLayout::WrapAllRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    for(auto* combo:group->findChildren<QComboBox*>()){
      combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);combo->setMinimumContentsLength(12);combo->setMinimumWidth(0);
    }
    for(auto* check:group->findChildren<QCheckBox*>()){
      check->setToolTip(check->text());check->setText(check->text().section(" (",0,0));
    }

    if(auto* box=qobject_cast<QGroupBox*>(group);box&&box->isCheckable())box->setChecked(true);
    auto* scroll=new QScrollArea(inputPages_);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto* page=new QWidget(scroll);auto* pageLayout=new QVBoxLayout(page);pageLayout->setContentsMargins(10,6,10,10);if(group!=filesSection_){auto* scope=new QLabel(tr("SI 전체 공통 설정 · 반응별 구조·기준·표는 구조 화면에서 설정합니다."),page);scope->setWordWrap(true);pageLayout->addWidget(scope);}pageLayout->addWidget(group,group==filesSection_?1:0);if(group!=filesSection_)pageLayout->addStretch();scroll->setWidget(page);inputPages_->addWidget(scroll);
  }
  inputScroll_=qobject_cast<QScrollArea*>(inputPages_->widget(0));delete content;return inputPages_;
}

QWidget* MainWindow::createWorkspace(){
  auto* panel=new QWidget(this);
  auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(6,6,6,6);
  auto* controls=new QHBoxLayout;controls->addWidget(new QLabel(tr("작업 미리보기"),panel));controls->addStretch();
  auto* focus=new QToolButton(panel);focus->setObjectName("focusWorkspace");
  focus->setText(tr("⛶ 크게 보기"));focus->setCheckable(true);
  focus->setToolTip(tr("왼쪽 패널을 접어 뷰어를 크게 표시합니다."));
  controls->addWidget(focus);layout->addLayout(controls);
  workspaceTabs_=new QTabWidget(panel);workspaceTabs_->setObjectName("workspaceTabs");
  workspaceTabs_->setDocumentMode(true);
  auto* molecularPage=new QWidget(workspaceTabs_);auto* moleculeLayout=new QVBoxLayout(molecularPage);
  moleculeView_=new MoleculeViewWidget(molecularPage);moleculeLayout->addWidget(moleculeView_,1);
  interactionInfo_=new QLabel(tr("원자를 클릭하여 정보를 확인하세요. (Click an atom for details.)"),molecularPage);
  interactionInfo_->setWordWrap(true);moleculeLayout->addWidget(interactionInfo_);
  workspaceTabs_->addTab(molecularPage,tr("3D 분자 뷰어"));
  auto* previewPage=new QWidget(workspaceTabs_);auto* previewLayout=new QVBoxLayout(previewPage);
  previewScope_=new QComboBox(previewPage);previewScope_->setObjectName("previewScope");
  previewScope_->addItem(tr("현재 반응 미리보기"),"current");
  previewScope_->addItem(tr("전체 SI 미리보기"),"all");
  previewLayout->addWidget(previewScope_);preview_=new DocumentPreviewWidget(previewPage);
  previewLayout->addWidget(preview_,1);workspaceTabs_->addTab(previewPage,tr("SI 문서 미리보기"));
  layout->addWidget(workspaceTabs_,1);
  connect(previewScope_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshScopedPreview();});
  connect(focus,&QToolButton::toggled,this,[this,focus](bool active){
    if(!mainSplitter_)return;
    workspaceFocused_=active;
    if(active){workspaceSplitState_=mainSplitter_->saveState();mainSplitter_->setSizes({0,0,std::max(1,mainSplitter_->width())});focus->setText(tr("기본 화면"));}
    else{if(!workspaceSplitState_.isEmpty())mainSplitter_->restoreState(workspaceSplitState_);focus->setText(tr("⛶ 크게 보기"));}
  });
  status_=new QLabel(tr(".out 또는 .log 파일을 끌어 놓으세요. (Drop Gaussian files here.)"),panel);
  status_->setWordWrap(true);layout->addWidget(status_);
  connect(moleculeView_,&MoleculeViewWidget::atomPicked,this,[this](int atom,const QString& element,double spin,bool found){
    interactionInfo_->setText(found?tr("%1 %2 | Spin density = %3").arg(element).arg(atom).arg(spin,0,'f',3):tr("%1 %2 | Spin density = N/A").arg(element).arg(atom));
  });
  connect(moleculeView_,&MoleculeViewWidget::geometrySelectionChanged,this,&MainWindow::updateGeometryFeedback);
  return panel;
}

QWidget* MainWindow::createQuickToolbar(){
  auto* bar=new QToolBar(tr("작업 도구"),this);bar->setObjectName("quickToolbar");bar->setMovable(false);bar->setFloatable(false);bar->setAllowedAreas(Qt::TopToolBarArea);bar->setIconSize(QSize(24,24));addToolBar(Qt::TopToolBarArea,bar);
  auto add=[this,bar](const QString& name,const QString& text,const QIcon& icon,auto slot){
    auto* button=new QToolButton(bar);button->setObjectName(name);button->setText(text);button->setAccessibleName(text);button->setToolTip(text);button->setIcon(icon);button->setIconSize(QSize(24,24));button->setToolButtonStyle(Qt::ToolButtonIconOnly);button->setFixedSize(42,42);connect(button,&QToolButton::clicked,this,slot);bar->addWidget(button);return button;
  };
  auto* manage=new QToolButton(bar);manage->setObjectName("projectManagerButton");manage->setText(tr("프로젝트 관리"));manage->setToolTip(tr("프로젝트를 저장하고 관리 창으로 돌아가기"));connect(manage,&QToolButton::clicked,this,&MainWindow::projectManagerRequested);bar->addWidget(manage);auto* brand=new QLabel("  SI Generator  ",bar);brand->setObjectName("toolbarBrand");bar->addWidget(brand);bar->addSeparator();bar->addWidget(new QLabel(tr("파일  "),bar));
  auto* addFiles=add("addFilesAction",tr("Gaussian 파일 추가 (Ctrl+O)"),style()->standardIcon(QStyle::SP_FileDialogNewFolder),[this]{chooseFiles();});
  addFiles->setText(tr("파일 추가"));addFiles->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);addFiles->setFixedWidth(105);
  add("openProjectAction",tr("프로젝트 열기"),style()->standardIcon(QStyle::SP_DirOpenIcon),[this]{openProject();});
  auto* save=add("saveProjectAction",tr("프로젝트 저장 (Ctrl+S)"),style()->standardIcon(QStyle::SP_DialogSaveButton),[this]{saveProject();});
  save->setText(tr("저장"));save->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);save->setFixedWidth(90);
  bar->addSeparator();bar->addWidget(new QLabel(tr("설정  "),bar));
  navigation_=new QButtonGroup(this);navigation_->setExclusive(true);
  const QStringList names={"structures","energy","spin","geometry","document"};
  const QStringList labels={tr("구조와 파일"),tr("에너지와 기준점"),tr("스핀 밀도"),tr("거리·각도·이면각"),tr("문서와 출력")};
  const QList<QWidget*> sections={filesSection_,energySection_,spinSection_,geometrySection_,documentSection_};
  for(int i=0;i<sections.size();++i){
    auto* button=add("navigate_"+names[i],labels[i]+tr(" (Alt+%1)").arg(i+1),navigationIcon(names[i],QColor("#365b85"),i+1),[this,sections,i]{
      scrollTo(sections[i]);
      if(i==2)moleculeView_->setInteractionMode(MoleculeViewWidget::InteractionMode::SpinDensity);
      if(i==3)moleculeView_->setInteractionMode(MoleculeViewWidget::InteractionMode::Geometry);
      if(i!=2&&i!=3)moleculeView_->setInteractionMode(MoleculeViewWidget::InteractionMode::View);
    });
    button->setCheckable(true);button->setProperty("navigation",true);button->setShortcut(QKeySequence(QString("Alt+%1").arg(i+1)));navigation_->addButton(button,i);
  }
  navigation_->button(0)->setChecked(true);bar->addSeparator();bar->addWidget(new QLabel(tr("분자  "),bar));
  auto* indices=add("toggleAtomIndices",tr("원자 번호 표시 / 숨기기"),navigationIcon("indices",QColor("#746197")),[this]{moleculeView_->setAtomIndicesVisible(!moleculeView_->atomIndicesVisible());});indices->setCheckable(true);indices->setChecked(moleculeView_->atomIndicesVisible());
  add("resetMolecularView",tr("분자 보기 초기화"),navigationIcon("reset",QColor("#746197")),[this]{moleculeView_->resetView();});
  add("prepareFiguresAction",tr("분자 Figure 뷰 설정"),navigationIcon("figure",QColor("#746197")),[this]{prepareFigures();});
  bar->addSeparator();bar->addWidget(new QLabel(tr("내보내기  "),bar));
  auto* generateSi=add("generateSiButton",tr("전체 SI DOCX 생성"),navigationIcon("si",QColor("#087e8b")),[this]{exportSiDocument();});
  generateSi->setText(tr("Generate SI + XYZ"));generateSi->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);generateSi->setFixedWidth(185);
  auto* currentSection=new QLabel(labels[0],bar);currentSection->setObjectName("currentSectionLabel");bar->addWidget(currentSection);
  connect(inputPages_,&QStackedWidget::currentChanged,this,[this,currentSection,labels](int index){inputScroll_=qobject_cast<QScrollArea*>(inputPages_->widget(index));navigation_->button(index)->setChecked(true);currentSection->setText(labels[index]);});
  return bar;
}

void MainWindow::connectPreviewInputs(){const auto edits=findChildren<QLineEdit*>();for(auto* edit:edits)connect(edit,&QLineEdit::textChanged,this,&MainWindow::scheduleDocumentRefresh);const auto texts=findChildren<QTextEdit*>();for(auto* edit:texts)connect(edit,&QTextEdit::textChanged,this,&MainWindow::scheduleDocumentRefresh);for(auto* combo:findChildren<QComboBox*>())connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::scheduleDocumentRefresh);for(auto* spin:findChildren<QSpinBox*>())connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::scheduleDocumentRefresh);for(auto* check:findChildren<QCheckBox*>())connect(check,&QCheckBox::toggled,this,&MainWindow::scheduleDocumentRefresh);}
void MainWindow::scrollTo(QWidget* target){
  for(int i=0;i<inputPages_->count();++i)if(inputPages_->widget(i)->isAncestorOf(target)){inputPages_->setCurrentIndex(i);break;}
  if(auto* box=qobject_cast<QGroupBox*>(target);box&&box->isCheckable())box->setChecked(true);
  if(target->parentWidget()!=inputScroll_->widget())inputScroll_->ensureWidgetVisible(target,10,10);
}
