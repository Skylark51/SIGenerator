#include "MainWindow.hpp"
#include "DocxWriter.hpp"
#include "DocumentPreviewWidget.hpp"
#include "MoleculeViewWidget.hpp"
#include "ProjectStore.hpp"
#include "FigureSetupDialog.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include "si/SIModel.hpp"
#include "si/StructureLabels.hpp"
#include "si/XyzWriter.hpp"

#include <QCheckBox>
#include <QFile>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
QSettings windowSettings(){return QSettings(QStringLiteral("SI Generator"),qEnvironmentVariableIsSet("SI_GENERATOR_TEST_SETTINGS")?QStringLiteral("SIGeneratorTests"):QStringLiteral("SIGenerator"));}
QStringList lines(const QString& text){return text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),Qt::SkipEmptyParts);}
QVector<QVector<int>> indexGroups(const QString& text){QVector<QVector<int>> groups;for(const auto& raw:text.split(QRegularExpression(QStringLiteral("[\\r\\n|]+")),Qt::SkipEmptyParts)){QVector<int> group;for(const auto& token:raw.split(QRegularExpression(QStringLiteral("[^0-9]+")),Qt::SkipEmptyParts))group.append(token.toInt());if(!group.isEmpty())groups.append(group);}return groups;}
QVector<QStringList> valueRows(const QString& text){QVector<QStringList> rows;for(const auto& line:lines(text)){auto values=line.trimmed().split(QRegularExpression(QStringLiteral("[\\t ]+")),Qt::SkipEmptyParts);rows.append(values);}return rows;}
QString number(const std::optional<double>& value,int digits,int precision){return si::formatNumber(value,digits,precision);}
QString manualNumber(const QString& value,int precision){if(precision<=0)return value;bool ok=false;const double parsed=value.toDouble(&ok);return ok?si::formatNumber(parsed,precision,precision):value;}
void filterRelativeTable(si::DocumentTable& table,bool dispersion,bool complexation){const auto columns=si::relativeEnergyColumns(dispersion,complexation);QStringList headers;QVector<QStringList> rows;for(int logical:columns)headers.append(table.headers[logical+1]);for(const auto& source:table.rows){QStringList row{source.value(0)};for(int logical:columns)row.append(source.value(logical+1));rows.append(row);}table.headers=QStringList{QString()}+headers;table.rows=rows;table.boldColumns.clear();for(int i=0;i<table.headers.size();++i)if(table.headers[i]==QStringLiteral("E_{Total}")||table.headers[i]==QStringLiteral("ΔG^{c}"))table.boldColumns.append(i);}
QString pairedSinglePointPath(const QString& frequencyPath){return QString::fromStdWString(si::pairedSinglePointPath(std::filesystem::path(frequencyPath.toStdWString())).wstring());}
QString atomLabel(const si::GaussianResult& result,int atomId){if(atomId>0&&static_cast<size_t>(atomId)<=result.finalGeometry.size())return QString::fromStdString(result.finalGeometry[static_cast<size_t>(atomId-1)].symbol)+QStringLiteral("(")+QString::number(atomId)+QStringLiteral(")");return QStringLiteral("At(%1)").arg(atomId);}
QGroupBox* section(const QString& title,QWidget* parent,bool collapsible=false){auto* box=new QGroupBox(title,parent);if(collapsible){box->setCheckable(true);box->setChecked(false);QObject::connect(box,&QGroupBox::toggled,box,[box](bool open){for(auto* child:box->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))child->setVisible(open);});}return box;}
}

MainWindow::MainWindow(){
  setObjectName(QStringLiteral("mainWindow"));setAcceptDrops(true);setWindowTitle(tr("SI Generator — 네이티브 (Native)"));resize(1480,900);
  auto* fileMenu=menuBar()->addMenu(tr("파일 (File)"));fileMenu->addAction(tr("Gaussian 파일 열기 (Open Files)…"),QKeySequence::Open,this,&MainWindow::chooseFiles);fileMenu->addAction(tr("프로젝트 열기 (Open Project)…"),this,&MainWindow::openProject);fileMenu->addAction(tr("프로젝트 저장 (Save Project)…"),QKeySequence::Save,this,&MainWindow::saveProject);
  auto* splitter=new QSplitter(Qt::Horizontal,this);splitter->setObjectName(QStringLiteral("mainSplitter"));splitter->addWidget(createInputPanel());splitter->addWidget(createWorkspace());splitter->addWidget(createQuickToolbar());splitter->setStretchFactor(0,27);splitter->setStretchFactor(1,65);splitter->setStretchFactor(2,8);splitter->setSizes({360,900,130});setCentralWidget(splitter);
  previewTimer_=new QTimer(this);previewTimer_->setSingleShot(true);previewTimer_->setInterval(220);connect(previewTimer_,&QTimer::timeout,this,&MainWindow::refreshDocumentModel);connectPreviewInputs();
  auto settings=windowSettings();baseDirectory_->setText(settings.value(QStringLiteral("baseDirectory")).toString());restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
  {QSignalBlocker a(referencesText_),b(computationalDetails_);referencesText_->setPlainText(settings.value("documentDraft/referencesText").toString());
   detailsEdited_=settings.value("documentDraft/detailsEdited",false).toBool();if(detailsEdited_)computationalDetails_->setPlainText(settings.value("documentDraft/computationalDetails").toString());}
  connect(referencesText_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();const auto value=referencesText_->toPlainText();draft.setValue("documentDraft/referencesText",value);if(!value.trimmed().isEmpty())draft.setValue("documentDraft/lastNonemptyReferences",value);});
  connect(computationalDetails_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();draft.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());draft.setValue("documentDraft/detailsEdited",detailsEdited_);});
  scheduleDocumentRefresh();
}

MainWindow::~MainWindow(){if(importWatcher_)importWatcher_->cancel();auto settings=windowSettings();settings.setValue(QStringLiteral("baseDirectory"),baseDirectory_->text());settings.setValue(QStringLiteral("geometry"),saveGeometry());settings.setValue("documentDraft/referencesText",referencesText_->toPlainText());settings.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());settings.setValue("documentDraft/detailsEdited",detailsEdited_);}

QWidget* MainWindow::createInputPanel(){
  inputScroll_=new QScrollArea(this);inputScroll_->setObjectName(QStringLiteral("inputPanel"));inputScroll_->setWidgetResizable(true);inputScroll_->setMinimumWidth(310);auto* content=new QWidget(inputScroll_);auto* layout=new QVBoxLayout(content);layout->setContentsMargins(10,10,10,10);layout->setSpacing(8);
  auto* heading=new QLabel(tr("입력 (Input)"),content);QFont headingFont=heading->font();headingFont.setPointSize(16);headingFont.setBold(true);heading->setFont(headingFont);layout->addWidget(heading);
  auto* filesBox=section(tr("구조와 Gaussian 파일 (Structures & Gaussian Files)"),content);auto* filesLayout=new QVBoxLayout(filesBox);auto* fileButtons=new QHBoxLayout;auto* add=new QPushButton(tr("파일 추가 (Add Files)"),filesBox);auto* folder=new QPushButton(tr("폴더 추가 (Folder)"),filesBox);connect(add,&QPushButton::clicked,this,&MainWindow::chooseFiles);connect(folder,&QPushButton::clicked,this,&MainWindow::chooseDirectory);fileButtons->addWidget(add);fileButtons->addWidget(folder);filesLayout->addLayout(fileButtons);baseDirectory_=new QLineEdit(filesBox);baseDirectory_->setPlaceholderText(tr("기본 폴더 (Base Folder)"));filesLayout->addWidget(baseDirectory_);files_=new QTableWidget(0,7,filesBox);files_->setObjectName(QStringLiteral("structureList"));files_->setHorizontalHeaderLabels({"Structure","Mult.","Freq","SPE","Imag.","Spin","Status"});files_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);files_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);files_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);files_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);for(int column=1;column<7;++column)files_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);files_->setSelectionBehavior(QAbstractItemView::SelectRows);files_->setSelectionMode(QAbstractItemView::SingleSelection);files_->setEditTriggers(QAbstractItemView::NoEditTriggers);files_->setMinimumHeight(180);connect(files_,&QTableWidget::itemSelectionChanged,this,&MainWindow::selectStructure);filesLayout->addWidget(files_);auto* editRow=new QHBoxLayout;auto* rename=new QPushButton(tr("이름 변경 (Rename)"),filesBox);auto* remove=new QPushButton(tr("제거 (Remove)"),filesBox);connect(rename,&QPushButton::clicked,this,&MainWindow::renameSelected);connect(remove,&QPushButton::clicked,this,&MainWindow::removeSelected);editRow->addWidget(rename);editRow->addWidget(remove);filesLayout->addLayout(editRow);
  fileNames_=new QTextEdit(filesBox);fileNames_->setObjectName("fileNamesInput");fileNames_->setAcceptRichText(false);
  fileNames_->setPlaceholderText(tr("엑셀 파일명 셀을 복사하여 붙여넣기 (Ctrl+V)\n라벨 [탭] 파일이름 — 엑셀 두 열 그대로 붙여넣기\n예: 5DCR-TS    calculation.out · 괄호 불필요"));fileNames_->setFixedHeight(104);fileNames_->setLineWrapMode(QTextEdit::NoWrap);
  filesLayout->insertWidget(2,fileNames_);
  auto* addNames=new QPushButton(tr("파일명으로 추가 (Add by Names)"),filesBox);addNames->setObjectName("addNamedFiles");
  connect(addNames,&QPushButton::clicked,this,[this]{addNamedFiles();});filesLayout->insertWidget(3,addNames);
  auto* replaceNames=new QPushButton(tr("입력 목록으로 교체 (라벨·순서 그대로)"),filesBox);replaceNames->setObjectName("replaceNamedFiles");replaceNames->setToolTip(tr("입력한 파일만 입력한 순서대로 사용합니다. 자동으로 추가된 IRC 행을 제거할 때 사용할 수 있습니다."));filesLayout->insertWidget(4,replaceNames);
  connect(replaceNames,&QPushButton::clicked,this,[this]{addNamedFiles(true);});
  fileNamesInfo_=new QLabel(filesBox);fileNamesInfo_->setWordWrap(true);filesLayout->insertWidget(4,fileNamesInfo_);
  folder->setText(tr("폴더 지정 (Set Folder)"));folder->setToolTip(tr("기본 경로만 지정합니다. 파일은 직접 선택하거나 파일명으로 추가하세요."));
  auto* orderRow=new QHBoxLayout;
  auto* up=new QPushButton(tr("위로 (Up)"),filesBox);up->setObjectName("moveStructureUp");
  auto* down=new QPushButton(tr("아래로 (Down)"),filesBox);down->setObjectName("moveStructureDown");
  auto* position=new QPushButton(tr("위치 지정 (Move To)"),filesBox);position->setObjectName("moveStructureTo");
  orderRow->addWidget(up);orderRow->addWidget(down);orderRow->addWidget(position);filesLayout->addLayout(orderRow);
  connect(up,&QPushButton::clicked,this,[this]{moveSelectedStructure(files_->currentRow()-1);});
  connect(down,&QPushButton::clicked,this,[this]{moveSelectedStructure(files_->currentRow()+1);});
  connect(position,&QPushButton::clicked,this,[this]{if(parsing_||files_->currentRow()<0)return;bool ok=false;const int target=QInputDialog::getInt(this,tr("파일 순서 변경"),tr("이동할 위치 (1–%1)").arg(files_->rowCount()),files_->currentRow()+1,1,files_->rowCount(),1,&ok);if(ok)moveSelectedStructure(target-1);});
  auto updateOrderButtons=[this,up,down,position]{const int row=files_->currentRow();up->setEnabled(!parsing_&&row>0);down->setEnabled(!parsing_&&row>=0&&row<files_->rowCount()-1);position->setEnabled(!parsing_&&row>=0&&files_->rowCount()>1);};
  connect(files_,&QTableWidget::itemSelectionChanged,this,updateOrderButtons);updateOrderButtons();structureLabel_=new QLineEdit(filesBox);structureLabel_->setPlaceholderText(tr("선택 구조 이름 (예: 5DCR-TS)"));connect(structureLabel_,&QLineEdit::editingFinished,this,&MainWindow::renameSelected);filesLayout->addWidget(structureLabel_);layout->addWidget(filesBox);
  energySection_=section(tr("에너지와 기준점 (Energy & Reference)"),content);auto* energy=new QFormLayout(energySection_);referenceMode_=new QComboBox(energySection_);referenceMode_->addItem(tr("단일 구조 (Single Structure)"),QStringLiteral("single"));referenceMode_->addItem(tr("수동 기준값 (Manual Reference)"),QStringLiteral("manual_numeric"));referenceMode_->addItem(tr("수동 상대값만 (Manual Relative Only)"),QStringLiteral("manual_relative_only"));energy->addRow(tr("기준 방식 (Reference Mode)"),referenceMode_);reference_=new QComboBox(energySection_);reference_->setObjectName("referenceStructure");energy->addRow(tr("기준 구조 (Reference Structure)"),reference_);manualReference_=new QLineEdit(energySection_);manualReference_->setPlaceholderText(tr("Hartree"));energy->addRow(tr("수동 기준값 (Manual Reference)"),manualReference_);energyBasis_=new QComboBox(energySection_);energyBasis_->addItem(tr("기존 Python 방식 (성분별 SI 에너지)"),static_cast<int>(si::EnergyBasis::LegacyComposite));energyBasis_->addItem(tr("Composite Gibbs: E_SPE + (G_freq - E_freq)"),static_cast<int>(si::EnergyBasis::DefaultComposite));energyBasis_->addItem(tr("전자 에너지 (Electronic)"),static_cast<int>(si::EnergyBasis::Electronic));energyBasis_->addItem(tr("전자 + ZPE (Electronic + ZPE)"),static_cast<int>(si::EnergyBasis::ElectronicZpe));energyBasis_->addItem(tr("엔탈피 (Enthalpy)"),static_cast<int>(si::EnergyBasis::Enthalpy));energyBasis_->addItem(tr("Gibbs 자유에너지 (Gibbs)"),static_cast<int>(si::EnergyBasis::Gibbs));energy->addRow(tr("에너지 기준 (Energy Basis)"),energyBasis_);energyInputMode_=new QComboBox(energySection_);energyInputMode_->addItem(tr("자동 추출 (Automatic)"),QStringLiteral("automatic"));energyInputMode_->addItem(tr("수동 입력 (Manual)"),QStringLiteral("manual"));energy->addRow(tr("입력 방식 (Input Mode)"),energyInputMode_);includeAbsolute_=new QCheckBox(tr("절대 에너지 표 포함 (Include Absolute Energy)"),energySection_);includeAbsolute_->setChecked(false);energy->addRow(includeAbsolute_);includeDispersion_=new QCheckBox(tr("상대 에너지에 분산 보정 포함 (Include Dispersion)"),energySection_);includeDispersion_->setChecked(true);energy->addRow(includeDispersion_);includeComplexation_=new QCheckBox(tr("상대 에너지에 착물화 보정 포함 (Include Complexation)"),energySection_);includeComplexation_->setChecked(true);energy->addRow(includeComplexation_);manualAbsoluteNames_=new QTextEdit(energySection_);manualAbsoluteNames_->setPlaceholderText(tr("절대 에너지 구조명 — 한 줄에 하나 (Absolute Names)"));manualAbsoluteNames_->setFixedHeight(48);energy->addRow(manualAbsoluteNames_);manualAbsoluteValues_=new QTextEdit(energySection_);manualAbsoluteValues_->setPlaceholderText(tr("절대 에너지 값 — 공백/탭 구분 (Absolute Values)"));manualAbsoluteValues_->setFixedHeight(48);energy->addRow(manualAbsoluteValues_);manualRelativeNames_=new QTextEdit(energySection_);manualRelativeNames_->setPlaceholderText(tr("상대 에너지 구조명 (Relative Names)"));manualRelativeNames_->setFixedHeight(48);energy->addRow(manualRelativeNames_);manualRelativeValues_=new QTextEdit(energySection_);manualRelativeValues_->setPlaceholderText(tr("상대 에너지 값 (Relative Values)"));manualRelativeValues_->setFixedHeight(48);energy->addRow(manualRelativeValues_);
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
  spinSection_=section(tr("Mulliken 스핀 밀도 (Spin Density)"),content,true);auto* spin=new QFormLayout(spinSection_);spinNames_=new QTextEdit(spinSection_);spinNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));spinNames_->setFixedHeight(45);spin->addRow(spinNames_);spinIndices_=new QTextEdit(spinSection_);spinIndices_->setPlaceholderText(tr("원자 ID — 비우면 절댓값 상위 6개 자동 선택 (Atom IDs)"));spinIndices_->setFixedHeight(45);spin->addRow(spinIndices_);layout->addWidget(spinSection_);
  geometrySection_=section(tr("선택 구조 (Selected Geometry)"),content,true);auto* geometry=new QFormLayout(geometrySection_);geometryNames_=new QTextEdit(geometrySection_);geometryNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));geometryNames_->setFixedHeight(45);geometry->addRow(geometryNames_);geometryIndices_=new QTextEdit(geometrySection_);geometryIndices_->setPlaceholderText(tr("2/3/4개 원자 ID: 거리/각/이면각 — 비우면 RMSD 자동 분석"));geometryIndices_->setFixedHeight(55);geometry->addRow(geometryIndices_);geometryAutoInfo_=new QLabel(geometrySection_);geometryAutoInfo_->setWordWrap(true);geometryAutoInfo_->setObjectName("geometryAutoInfo");geometry->addRow(geometryAutoInfo_);auto* addGeometry=new QPushButton(tr("클릭 선택 추가 (Add Click Selection)"),geometrySection_);connect(addGeometry,&QPushButton::clicked,this,&MainWindow::appendSelectedGeometry);geometry->addRow(addGeometry);layout->addWidget(geometrySection_);
  auto* documentBox=section(tr("문서와 출력 (Document & Output)"),content);auto* document=new QFormLayout(documentBox);documentTitle_=new QLineEdit(QStringLiteral("Supporting Information"),documentBox);document->addRow(tr("문서 제목 (Document Title)"),documentTitle_);mechanismName_=new QLineEdit(documentBox);mechanismName_->setPlaceholderText(tr("반응/메커니즘 이름 (Mechanism Name)"));document->addRow(tr("메커니즘 (Mechanism)"),mechanismName_);decimalPlaces_=new QSpinBox(documentBox);decimalPlaces_->setRange(0,10);decimalPlaces_->setSpecialValueText(tr("권장 정밀도 (Recommended)"));document->addRow(tr("소수점 자리수 (Decimal Places)"),decimalPlaces_);captions_=new QTextEdit(documentBox);captions_->setPlaceholderText(tr("그림 캡션 — Figure 그룹당 한 줄 (첫 3개, 이후 2개)"));captions_->setFixedHeight(60);document->addRow(captions_);siOutputPath_=new QLineEdit(documentBox);siOutputPath_->setPlaceholderText(tr("SI DOCX 출력 경로 (SI Output Path)"));document->addRow(siOutputPath_);xyzOutputPath_=new QLineEdit(documentBox);xyzOutputPath_->setPlaceholderText(tr("XYZ 출력 경로 (*.xyz)"));document->addRow(xyzOutputPath_);manuscriptTitle_=new QLineEdit(documentBox);manuscriptTitle_->setObjectName("manuscriptTitle");document->addRow("Manuscript title",manuscriptTitle_);
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
  auto* restoreRefs=new QPushButton(tr("이전 References 입력 복원"),documentBox);restoreRefs->setObjectName("restoreReferenceDraft");document->addRow(restoreRefs);
  connect(restoreRefs,&QPushButton::clicked,this,[this]{auto draft=windowSettings();const auto value=draft.value("documentDraft/lastNonemptyReferences").toString();if(!value.isEmpty())referencesText_->setPlainText(value);else status_->setText(tr("저장된 이전 References 입력이 없습니다."));});
  auto* originalRefs=new QPushButton(tr("원본 SI의 계산 문헌 S5–S7 불러오기"),documentBox);originalRefs->setObjectName("restoreOriginalReferences");document->addRow(originalRefs);
  connect(originalRefs,&QPushButton::clicked,this,[this]{QFile file(":/si/recovered_computational_references.txt");if(file.open(QIODevice::ReadOnly)){referencesText_->setPlainText(QString::fromUtf8(file.readAll()));status_->setText(tr("SI_261027_revised.docx의 S5–S7을 불러왔습니다. 원문은 Gaussian 09/6-31G* 기준이므로 현재 계산의 버전·basis 문헌은 확인하세요."));}});
  auto* projects=new QHBoxLayout;auto* save=new QPushButton(tr("저장 (Save Project)"),documentBox);auto* open=new QPushButton(tr("불러오기 (Load Project)"),documentBox);connect(save,&QPushButton::clicked,this,&MainWindow::saveProject);connect(open,&QPushButton::clicked,this,&MainWindow::openProject);projects->addWidget(save);projects->addWidget(open);document->addRow(projects);layout->addWidget(documentBox);layout->addStretch();inputScroll_->setWidget(content);return inputScroll_;
}

QWidget* MainWindow::createWorkspace(){auto* panel=new QWidget(this);auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(6,6,6,6);auto* split=new QSplitter(Qt::Vertical,panel);split->setObjectName(QStringLiteral("workspaceSplitter"));auto* moleculeBox=new QGroupBox(tr("분자 렌더링 (Molecular Viewer)"),split);auto* moleculeLayout=new QVBoxLayout(moleculeBox);moleculeView_=new MoleculeViewWidget(moleculeBox);moleculeLayout->addWidget(moleculeView_,1);interactionInfo_=new QLabel(tr("원자를 클릭하여 정보를 확인하세요. (Click an atom for details.)"),moleculeBox);interactionInfo_->setWordWrap(true);moleculeLayout->addWidget(interactionInfo_);auto* previewBox=new QGroupBox(tr("SI 미리보기 (SI Preview)"),split);auto* previewLayout=new QVBoxLayout(previewBox);preview_=new DocumentPreviewWidget(previewBox);previewLayout->addWidget(preview_);split->addWidget(moleculeBox);split->addWidget(previewBox);split->setStretchFactor(0,65);split->setStretchFactor(1,35);split->setSizes({570,300});layout->addWidget(split,1);status_=new QLabel(tr(".out 또는 .log 파일을 끌어 놓으세요. (Drop Gaussian files here.)"),panel);status_->setWordWrap(true);layout->addWidget(status_);connect(moleculeView_,&MoleculeViewWidget::atomPicked,this,[this](int atom,const QString& element,double spin,bool found){interactionInfo_->setText(found?tr("%1 %2 | Spin density = %3").arg(element).arg(atom).arg(spin,0,'f',3):tr("%1 %2 | Spin density = N/A").arg(element).arg(atom));});connect(moleculeView_,&MoleculeViewWidget::geometrySelectionChanged,this,&MainWindow::updateGeometryFeedback);return panel;}

QWidget* MainWindow::createQuickToolbar(){auto* panel=new QWidget(this);panel->setObjectName(QStringLiteral("quickToolbar"));panel->setMinimumWidth(118);auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(5,8,5,8);auto add=[this,layout,panel](const QString& text,const QString& tip,const char* tone,auto slot){auto* button=new QToolButton(panel);button->setText(text);button->setToolTip(tip);button->setProperty("tone",tone);button->setToolButtonStyle(Qt::ToolButtonTextOnly);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);button->setMinimumHeight(52);connect(button,&QToolButton::clicked,this,slot);layout->addWidget(button);return button;};add(tr("절대 에너지\n(Absolute)"),tr("절대 에너지 입력으로 이동 (Open Absolute Energy)"),"energy",[this]{scrollTo(energySection_);});add(tr("상대 에너지\n(Relative)"),tr("상대 에너지 입력으로 이동 (Open Relative Energy)"),"energy",[this]{scrollTo(energySection_);});add(tr("스핀 밀도\n(Spin Density)"),tr("원자를 클릭하여 Mulliken spin density 확인"),"spin",[this]{moleculeView_->setInteractionMode(MoleculeViewWidget::InteractionMode::SpinDensity);scrollTo(spinSection_);});add(tr("선택 구조\n(Geometry)"),tr("원자 2/3/4개를 순서대로 선택"),"geometry",[this]{moleculeView_->setInteractionMode(MoleculeViewWidget::InteractionMode::Geometry);scrollTo(geometrySection_);});auto* indices=add(tr("원자 번호\n(Atom Index)"),tr("원자 번호 표시 전환 (Toggle Atom Indices)"),"view",[this]{moleculeView_->setAtomIndicesVisible(!moleculeView_->atomIndicesVisible());});indices->setCheckable(true);indices->setChecked(true);add(tr("보기 초기화\n(Reset View)"),tr("분자 보기 초기화 (Reset Molecular View)"),"view",[this]{moleculeView_->resetView();});layout->addStretch();auto* siButton=add(tr("Generate SI DOCX"),tr("현재 미리보기 데이터로 DOCX 생성"),"generate",[this]{exportSiDocument();});siButton->setObjectName(QStringLiteral("generateSiButton"));auto* coordinatesButton=add(tr("Generate Coordinates DOCX"),tr("Cartesian coordinate DOCX 생성"),"generate",[this]{exportCoordinatesDocument();});coordinatesButton->setObjectName("generateCoordinatesButton");auto* xyzButton=add(tr("Generate XYZ"),tr("실제 XYZ 텍스트 파일 생성"),"generate",[this]{exportXyzDocument();});xyzButton->setObjectName(QStringLiteral("generateXyzButton"));return panel;}

void MainWindow::connectPreviewInputs(){const auto edits=findChildren<QLineEdit*>();for(auto* edit:edits)connect(edit,&QLineEdit::textChanged,this,&MainWindow::scheduleDocumentRefresh);const auto texts=findChildren<QTextEdit*>();for(auto* edit:texts)connect(edit,&QTextEdit::textChanged,this,&MainWindow::scheduleDocumentRefresh);for(auto* combo:findChildren<QComboBox*>())connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::scheduleDocumentRefresh);for(auto* spin:findChildren<QSpinBox*>())connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::scheduleDocumentRefresh);for(auto* check:findChildren<QCheckBox*>())connect(check,&QCheckBox::toggled,this,&MainWindow::scheduleDocumentRefresh);}
void MainWindow::dragEnterEvent(QDragEnterEvent* event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent* event){
  QStringList paths;
  for(const auto& url:event->mimeData()->urls())if(url.isLocalFile()){
    paths.append(url.toLocalFile());
  }
  acceptPaths(paths);
  event->acceptProposedAction();
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

void MainWindow::addNamedFiles(bool replace){
  const QDir base(baseDirectory_->text().trimmed());
  if(baseDirectory_->text().trimmed().isEmpty()||!base.exists()){
    fileNamesInfo_->setText(tr("먼저 기본 폴더를 지정하세요."));return;
  }
  QStringList paths,problems;QHash<QString,QString> replacementLabels;
  auto resolve=[&](const QString& name,QString* problem){
    QString path=base.filePath(name);const auto suffix=QFileInfo(name).suffix();
    if(suffix.compare("out",Qt::CaseInsensitive)!=0&&suffix.compare("log",Qt::CaseInsensitive)!=0){
      const auto out=path+".out",log=path+".log";
      if(QFileInfo(out).isFile()&&QFileInfo(log).isFile()){
        if(problem)*problem=tr("확장자를 지정하세요 (.out/.log 모두 존재): %1").arg(name);return QString();
      }
      path=QFileInfo(out).isFile()?out:log;
    }
    if(QFileInfo(path).isFile())return QFileInfo(path).absoluteFilePath();
    if(problem)*problem=tr("파일 없음: %1").arg(name);return QString();
  };
  QVector<si::NamedStructureInput> entries;
  for(const auto& entry:si::namedStructureInputs(fileNames_->toPlainText())){
    // Preserve a two-column list of actual file names. An existing literal
    // whole name also takes precedence over the unparenthesized shorthand.
    if(entry.implicitPair&&resolve(entry.original,nullptr).isEmpty()&&!resolve(entry.label,nullptr).isEmpty()){
      entries.append({entry.label,entry.label,{}});entries.append({entry.fileName,entry.fileName,{}});
    }else entries.append(entry);
  }
  for(const auto& entry:entries){
    QString problem,label=entry.label;
    // A real file whose name contains parentheses still imports literally.
    QString path=!label.isEmpty()?resolve(entry.original,nullptr):QString();
    if(!path.isEmpty())label.clear();else path=resolve(entry.fileName,&problem);
    if(path.isEmpty()){problems.append(problem);continue;}
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

void MainWindow::parsePaths(const QList<QString>& incoming,bool append,bool discoverIrc){
  if(parsing_&&append){
    queuedImports_.append({incoming,discoverIrc});
    status_->setText(tr("해석 중 — 추가 요청 %1개 대기 중").arg(queuedImports_.size()));
    return;
  }
  // A project replacement invalidates every callback from the previous import.
  // Workers own their cache and never access the window after it is destroyed.
  if(!append){
    namedImportLabels_.clear();
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
    if(!auxiliary&&!seen.contains(absolute.toCaseFolded())&&info.isFile()&&(info.suffix().compare("out",Qt::CaseInsensitive)==0||info.suffix().compare("log",Qt::CaseInsensitive)==0)){
      paths.append(absolute);seen.insert(absolute.toCaseFolded());
    }
  }
  if(paths.isEmpty()&&append){status_->setText(tr("추가할 Gaussian 파일이 없습니다 (중복·보조 파일·경로 확인)."));if(!queuedImports_.isEmpty()){const auto queued=queuedImports_.takeFirst();parsePaths(queued.paths,true,queued.discoverIrc);}return;}
  const int firstRow=append?files_->rowCount():0;
  const int previousRow=append?files_->currentRow():-1;
  {
    QSignalBlocker blocked(files_);
    files_->setUpdatesEnabled(false);
    if(!append){files_->setRowCount(0);parsed_.clear();pairedPaths_.clear();moleculeView_->clearMolecule();}
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
  syncReferenceOptions();
  if(paths.isEmpty()){scheduleDocumentRefresh();status_->setText(tr("구조 목록이 비었습니다."));return;}
  parsing_=true;previewTimer_->stop();
  status_->setText(tr("0 / %1개 구조 해석 중…").arg(paths.size()));
  struct Result{QString path;std::shared_ptr<const si::GaussianResult> data;QString pairPath;std::shared_ptr<const si::GaussianResult> pair;};
  auto* watcher=new QFutureWatcher<Result>(this);importWatcher_=watcher;
  connect(watcher,&QFutureWatcher<Result>::resultReadyAt,this,[this,watcher,generation](int index){
    if(generation!=importGeneration_)return;
    const auto item=watcher->resultAt(index);
    // The user may have removed a pending row while the worker was reading it.
    for(int row=0;row<files_->rowCount();++row)if(files_->item(row,0)->data(Qt::UserRole).toString()==item.path){
      parsed_.insert(item.path,item.data);pairedPaths_.insert(item.path,item.pairPath);
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
    status_->setText(tr("총 %1개 구조 해석 완료. Status에 마우스를 올리면 문제를 확인할 수 있습니다.").arg(files_->rowCount()));
    if(!missingEndpoints.isEmpty())status_->setText(status_->text()+tr("\n연결 IRC 파일 없음: %1").arg(missingEndpoints.join(", ")));
    emit files_->itemSelectionChanged();
    if(!queuedImports_.isEmpty()){
      const auto queued=queuedImports_.takeFirst();parsePaths(queued.paths,true,queued.discoverIrc);
    }
    scheduleDocumentRefresh();
  });
  const auto cache=cache_;
  watcher->setFuture(QtConcurrent::run([cache,paths](QPromise<Result>& promise){
    promise.setProgressRange(0,paths.size());
    auto parse=[cache](const QString& path)->std::shared_ptr<const si::GaussianResult>{
      try{return cache->get(path.toStdWString());}
      catch(const std::exception& error){auto failed=std::make_shared<si::GaussianResult>();failed->path=path.toStdWString();failed->success=false;failed->reason=error.what();return failed;}
    };
    for(int i=0;i<paths.size();++i){
      if(promise.isCanceled())return;
      const auto& path=paths[i];Result item{path,parse(path),{},{}};
      try{item.pairPath=pairedSinglePointPath(path);if(!item.pairPath.isEmpty())item.pair=parse(item.pairPath);}
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
std::vector<si::EnergyRecord> MainWindow::energyRecords() const{
  std::vector<si::EnergyRecord> records;
  for(int row=0;row<files_->rowCount();++row){
    const auto* item=files_->item(row,0);if(!item)continue;
    const auto path=item->data(Qt::UserRole).toString();const auto main=parsed_.value(path);if(!main)continue;
    si::EnergyRecord record;record.name=item->text().toStdString();record.frequency=*main;
    if(const auto pair=parsed_.value(pairedPaths_.value(path));pair&&pair->success)record.singlePoint=*pair;
    records.push_back(std::move(record));
  }
  return records;
}
void MainWindow::refreshPreflight(){
  auto records=energyRecords();geometryAutoInfo_->clear();
  const auto basis=static_cast<si::EnergyBasis>(energyBasis_->currentData().toInt());
  QStringList conditionWarnings;
  (void)si::computationalDetails(records,basis,&conditionWarnings);
  for(int row=0;row<static_cast<int>(records.size());++row){
    auto& record=records[row];const auto& freq=record.frequency;QStringList errors,warnings=conditionWarnings;
    if(!freq.success)errors.append(QString::fromStdString(freq.reason));
    if(freq.finalGeometry.empty())errors.append("Final geometry missing");
    if(energyInputMode_->currentData().toString()=="automatic"&&!si::selectEnergy(record,basis))errors.append("Required energy missing for selected mode");
    if(!freq.normalTermination)warnings.append("Normal termination not confirmed");
    if(!freq.frequenciesPresent)warnings.append("Frequency data missing");
    if(!record.singlePoint||!record.singlePoint->scfEnergy)warnings.append("SPE missing");
    if(record.singlePoint){
      const auto& spe=*record.singlePoint;
      if(freq.finalGeometry.size()!=spe.finalGeometry.size())warnings.append("Primary/SPE atom count mismatch");
      if(freq.charge&&spe.charge&&freq.charge!=spe.charge)warnings.append("Primary/SPE charge mismatch");
      if(freq.multiplicity&&spe.multiplicity&&freq.multiplicity!=spe.multiplicity)warnings.append("Primary/SPE multiplicity mismatch");
      if(!spe.normalTermination)warnings.append("SPE normal termination not confirmed");
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
}
QStringList MainWindow::preflightErrors() const{
  QStringList errors;
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
  return errors;
}
bool MainWindow::validateExport(){
  refreshDocumentModel();const auto errors=preflightErrors();
  if(errors.isEmpty())return true;
  QMessageBox::warning(this,tr("Preflight Error"),errors.join('\n'));return false;
}
void MainWindow::selectStructure(){
  if(!pendingStructureLabels_.isEmpty()){
    for(int index=0;index<files_->rowCount()&&index<pendingStructureLabels_.size();++index)if(files_->item(index,0))files_->item(index,0)->setText(pendingStructureLabels_[index]);
    pendingStructureLabels_.clear();
  }
  const int row=files_->currentRow();if(row<0)return;auto* item=files_->item(row,0);if(!item)return;structureLabel_->setText(item->text());const auto result=parsed_.value(item->data(Qt::UserRole).toString());if(result)moleculeView_->setMolecule(result->finalGeometry,result->spinDensities,result->spinBasisLevel,result->spinFallback);scheduleDocumentRefresh();
}
void MainWindow::renameSelected(){const int row=files_->currentRow();if(row<0)return;QString name=structureLabel_->text().trimmed();if(name.isEmpty()){bool accepted=false;name=QInputDialog::getText(this,tr("구조 이름 (Structure Label)"),tr("구조 이름 (Structure Label):"),QLineEdit::Normal,files_->item(row,0)->text(),&accepted).trimmed();if(!accepted||name.isEmpty())return;}files_->item(row,0)->setText(name);syncReferenceOptions();scheduleDocumentRefresh();}
void MainWindow::removeSelected(){const int row=files_->currentRow();if(row<0)return;const auto path=files_->item(row,0)->data(Qt::UserRole).toString();files_->removeRow(row);parsed_.remove(path);if(files_->rowCount())files_->selectRow(std::min(row,files_->rowCount()-1));else moleculeView_->clearMolecule();syncReferenceOptions();scheduleDocumentRefresh();}
void MainWindow::moveSelectedStructure(int targetRow){
  const int from=files_->currentRow(),count=files_->rowCount();
  if(parsing_||from<0||targetRow<0||targetRow>=count||from==targetRow)return;
  // Keep per-structure user input attached to the structure it describes.
  auto moveText=[from,targetRow,count](QTextEdit* edit,bool allowPipe=false){
    auto rows=edit->toPlainText().split(QRegularExpression(allowPipe?QStringLiteral("[\\r\\n|]+"):QStringLiteral("[\\r\\n]+")),Qt::SkipEmptyParts);
    if(rows.size()==count){rows.move(from,targetRow);QSignalBlocker blocked(edit);edit->setPlainText(rows.join('\n'));}
  };
  moveText(spinNames_);moveText(spinIndices_,true);moveText(geometryNames_);
  {
    QSignalBlocker blocked(files_);QList<QTableWidgetItem*> items;
    for(int col=0;col<files_->columnCount();++col)items.append(files_->takeItem(from,col));
    files_->removeRow(from);files_->insertRow(targetRow);
    for(int col=0;col<items.size();++col)files_->setItem(targetRow,col,items[col]);
    files_->setCurrentCell(targetRow,0);files_->selectRow(targetRow);
  }
  syncReferenceOptions();selectStructure();
  // Selection signals were blocked while row items were moved.
  emit files_->itemSelectionChanged();
  refreshDocumentModel();
}
QStringList MainWindow::structureLabels() const{QStringList values;for(int row=0;row<files_->rowCount();++row)values.append(files_->item(row,0)?files_->item(row,0)->text():QStringLiteral("Str_%1").arg(row+1));return values;}
QStringList MainWindow::sourcePaths() const{QStringList values;for(int row=0;row<files_->rowCount();++row)if(auto* item=files_->item(row,0))values.append(item->data(Qt::UserRole).toString());return values;}

si::SIDocumentModel MainWindow::buildDocumentModel(){
  si::SIDocumentModel model;model.title=documentTitle_->text().trimmed().isEmpty()?QStringLiteral("Supporting Information"):documentTitle_->text().trimmed();model.mechanismName=mechanismName_->text().trimmed();model.figureCaptions=lines(captions_->toPlainText());model.precisionOverride=decimalPlaces_->value();const int precision=model.precisionOverride;const auto labels=structureLabels();const auto paths=sourcePaths();
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
  const auto signature=QCryptographicHash::hash(geometryBytes,QCryptographicHash::Sha256);
  if(signature!=figureSignature_){preparedFigures_.clear();figureSignature_=signature;}
  model.figures=preparedFigures_;
  if(energyInputMode_->currentData().toString()==QStringLiteral("manual")){
    if(includeAbsolute_->isChecked()){si::DocumentTable table{si::DocumentSection::AbsoluteEnergy,tr("Absolute energy values of %1 in Kcal/Mol.").arg(si::displayMechanism(model)),{QString(),QStringLiteral("E_{Def2-SVP}"),QStringLiteral("E_{Def2-TZVPP}"),QStringLiteral("Z_{0}"),QStringLiteral("E_{Thermal}"),QStringLiteral("S"),QStringLiteral("Dispersion")},{},{}};const auto names=lines(manualAbsoluteNames_->toPlainText());const auto rows=valueRows(manualAbsoluteValues_->toPlainText());for(int i=0;i<std::max(names.size(),rows.size());++i){QStringList row{i<names.size()?names[i]:QString()};if(i<rows.size())for(const auto& value:rows[i])row.append(manualNumber(value,precision));table.rows.append(row);}if(!table.rows.isEmpty())model.tables.append(table);}
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
    if(includeAbsolute_->isChecked()){
      si::DocumentTable table{si::DocumentSection::AbsoluteEnergy,
        tr("Absolute energy values of %1. Electronic and dispersion energies in Hartree; ZPE and thermal energy in kcal/mol; entropy in cal mol^{-1} K^{-1}.").arg(si::displayMechanism(model)),
        {QString(),"E_{Def2-SVP}","E_{Def2-TZVPP}","Z_{0}","E_{Thermal}","S","Dispersion"},{},{}};
      for(const auto& record:records){
        const auto values=si::legacyAbsoluteEnergyValues(record);
        QStringList row{QString::fromStdString(record.name)};
        for(int col=0;col<6;++col)row.append(number(values[col],col==0||col==1||col==5?8:3,precision));
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
      if(!manualRef)relative=si::legacyRelativeEnergies(records,records[refIndex],includeDispersion_->isChecked(),includeComplexation_->isChecked());
      else {
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
    if(!ids.isEmpty()&&completeSpinMapping){
      bool allTz=true;for(const auto& record:records)allTz=allTz&&record.frequency.spinBasisLevel=="TZVPP";
      QString caption=tr("Mulliken Spin Density Distributions of %1.").arg(si::displayMechanism(model));
      if(allTz)caption+=tr(" Calculated at the Def2-TZVPP level.");
      si::DocumentTable table{si::DocumentSection::SpinDensity,caption,{QString()},{},{}};
      int width=ids.size();for(const auto& group:groups)width=std::max(width,static_cast<int>(group.size()));
      for(int col=0;col<width;++col)table.headers.append(col<ids.size()?atomLabel(records.front().frequency,ids[col]):tr("Atom %1").arg(col+1));
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
          row.append(number(value,3,precision));
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
    if(!geometryGroups.isEmpty()){si::DocumentTable table{si::DocumentSection::SelectedGeometry,tr("Selected Geometries of %1 in Å and °.").arg(si::displayMechanism(model)),{QString()},{},{}};for(const auto& group:geometryGroups){QStringList atoms;for(const int id:group)atoms.append(atomLabel(*geometryLabels,id));table.headers.append(atoms.join('-')+QStringLiteral(" (%1)").arg(group.size()==2?QStringLiteral("Å"):QStringLiteral("°")));}const auto names=lines(geometryNames_->toPlainText());for(size_t index=0;index<records.size();++index){const auto& record=records[index];QStringList row{names.size()==static_cast<int>(records.size())?names[static_cast<int>(index)]:QString::fromStdString(record.name)};for(const auto& group:geometryGroups){if(automaticGeometry&&!compatibleGeometryRecords.contains(index)){row.append(QStringLiteral("N/A"));continue;}std::optional<double> value;if(group.size()==2)value=si::distance(record.frequency.finalGeometry,group[0],group[1]);else if(group.size()==3)value=si::angle(record.frequency.finalGeometry,group[0],group[1],group[2]);else if(group.size()==4)value=si::dihedral(record.frequency.finalGeometry,group[0],group[1],group[2],group[3]);row.append(number(value,3,precision));}table.rows.append(row);}model.tables.append(table);}
    QVector<QStringList> excited;QVector<QStringList> irc;for(const auto& record:records){for(const auto& state:record.frequency.excitedStates)excited.append({QString::fromStdString(record.name),QString::number(state.index),si::formatNumber(state.energyEv,4,precision),number(state.wavelengthNm,1,precision),number(state.oscillatorStrength,4,precision)});for(const auto& point:record.frequency.ircPoints)irc.append({QString::fromStdString(record.name),QString::number(point.index),QString::fromStdString(point.direction),number(point.scfEnergy,8,precision)});}if(!excited.isEmpty())model.tables.append({si::DocumentSection::Other,tr("TD-DFT excited states"),{tr("Structure"),tr("State"),tr("Energy (eV)"),tr("Wavelength (nm)"),tr("f")},excited,{}});if(!irc.isEmpty())model.tables.append({si::DocumentSection::Other,tr("IRC reaction-coordinate points"),{tr("Structure"),tr("Point"),tr("Direction"),tr("SCF energy (Hartree)")},irc,{}});
  }
  return model;
}

void MainWindow::scheduleDocumentRefresh(){if(previewTimer_)previewTimer_->start();}
void MainWindow::refreshDocumentModel(){if(parsing_)return;refreshPreflight();documentModel_=buildDocumentModel();preview_->setDocumentModel(documentModel_);}
void MainWindow::updateGeometryFeedback(const QVector<int>& ids){if(ids.isEmpty()){interactionInfo_->setText(tr("원자 2/3/4개를 순서대로 선택하세요. (Select 2/3/4 atoms.)"));return;}const int row=files_->currentRow();if(row<0)return;const auto result=parsed_.value(files_->item(row,0)->data(Qt::UserRole).toString());if(!result)return;QStringList labels;for(const int id:ids)labels.append(atomLabel(*result,id));QString value;if(ids.size()==2){const auto calculated=si::distance(result->finalGeometry,ids[0],ids[1]);if(calculated)value=QStringLiteral(" = %1 Å").arg(*calculated,0,'f',3);}else if(ids.size()==3){const auto calculated=si::angle(result->finalGeometry,ids[0],ids[1],ids[2]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}else if(ids.size()==4){const auto calculated=si::dihedral(result->finalGeometry,ids[0],ids[1],ids[2],ids[3]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}interactionInfo_->setText(labels.join(QStringLiteral(" – "))+value);}
void MainWindow::appendSelectedGeometry(){const auto ids=moleculeView_->selectedAtoms();if(ids.size()<2||ids.size()>4){QMessageBox::information(this,tr("선택 구조 (Selected Geometry)"),tr("원자 2개(거리), 3개(각), 또는 4개(이면각)를 선택하세요."));return;}QStringList tokens;for(const int id:ids)tokens.append(QString::number(id));auto text=geometryIndices_->toPlainText().trimmed();if(!text.isEmpty())text+=QChar('\n');geometryIndices_->setPlainText(text+tokens.join(' '));if(geometryNames_->toPlainText().trimmed().isEmpty())geometryNames_->setPlainText(structureLabels().join('\n'));moleculeView_->clearSelection();scheduleDocumentRefresh();}
void MainWindow::scrollTo(QWidget* target){if(auto* box=qobject_cast<QGroupBox*>(target);box&&box->isCheckable())box->setChecked(true);inputScroll_->ensureWidgetVisible(target,10,10);}

void MainWindow::saveProject(){refreshDocumentModel();const auto path=QFileDialog::getSaveFileName(this,tr("SI 프로젝트 저장 (Save Project)"),baseDirectory_->text()+QStringLiteral("/SI_Project.si_project.json"),tr("SI Project (*.si_project.json);;JSON (*.json)"));if(path.isEmpty())return;ProjectState state;state.files=sourcePaths();state.structureNames=structureLabels().join('\n');state.referenceMode=referenceMode_->currentData().toString();state.referencePath=reference_->currentData().toString();state.manualReference=manualReference_->text();state.energyInputMode=energyInputMode_->currentData().toString();state.energyBasis=QString::number(energyBasis_->currentData().toInt());state.manualAbsoluteNames=manualAbsoluteNames_->toPlainText();state.manualAbsoluteValues=manualAbsoluteValues_->toPlainText();state.manualRelativeNames=manualRelativeNames_->toPlainText();state.manualRelativeValues=manualRelativeValues_->toPlainText();state.spinNames=spinNames_->toPlainText();state.spinIndices=spinIndices_->toPlainText();state.geometryNames=geometryNames_->toPlainText();state.geometryIndices=geometryIndices_->toPlainText();state.captions=captions_->toPlainText();state.mechanismName=mechanismName_->text();state.documentTitle=documentTitle_->text();state.manuscriptTitle=manuscriptTitle_->text();state.authors=authors_->text();state.affiliations=affiliations_->text();state.correspondingEmail=correspondingEmail_->text();state.computationalDetails=computationalDetails_->toPlainText();state.computationalDetailsEdited=detailsEdited_;state.referencesText=referencesText_->toPlainText();state.figureSettings=figureSettings_;state.includeAbsolute=includeAbsolute_->isChecked();state.decimalPlaces=decimalPlaces_->value();state.includeDispersion=includeDispersion_->isChecked();state.includeComplexation=includeComplexation_->isChecked();state.baseDirectory=baseDirectory_->text();state.siPath=siOutputPath_->text();state.xyzPath=xyzOutputPath_->text();QString error;if(!ProjectStore::save(path,state,&error))QMessageBox::critical(this,tr("저장 실패 (Save Failed)"),error);else status_->setText(tr("프로젝트 저장됨: %1").arg(path));}
void MainWindow::openProject(){
  const auto path=QFileDialog::getOpenFileName(this,tr("SI 프로젝트 열기 (Open Project)"),baseDirectory_->text(),tr("SI Project (*.si_project.json *.json)"));if(path.isEmpty())return;
  ProjectState state;QString error;if(!ProjectStore::load(path,&state,&error)){QMessageBox::critical(this,tr("열기 실패 (Open Failed)"),error);return;}
  pendingStructureLabels_=lines(state.structureNames);pendingLabelsByPath_.clear();for(int i=0;i<state.files.size();++i)pendingLabelsByPath_.insert(QFileInfo(state.files[i]).absoluteFilePath(),pendingStructureLabels_.value(i,QFileInfo(state.files[i]).completeBaseName()));
  referenceMode_->setCurrentIndex(std::max(0,referenceMode_->findData(state.referenceMode)));pendingReferencePath_=state.referencePath;manualReference_->setText(state.manualReference);energyInputMode_->setCurrentIndex(std::max(0,energyInputMode_->findData(state.energyInputMode)));bool ok=false;const int basis=state.energyBasis.toInt(&ok);if(ok&&basis==static_cast<int>(si::EnergyBasis::LegacyComposite)&&energyBasis_->findData(basis)<0)energyBasis_->addItem("Legacy composite (saved project)",basis);if(ok)energyBasis_->setCurrentIndex(std::max(0,energyBasis_->findData(basis)));manualAbsoluteNames_->setPlainText(state.manualAbsoluteNames);manualAbsoluteValues_->setPlainText(state.manualAbsoluteValues);manualRelativeNames_->setPlainText(state.manualRelativeNames);manualRelativeValues_->setPlainText(state.manualRelativeValues);spinNames_->setPlainText(state.spinNames);spinIndices_->setPlainText(state.spinIndices);geometryNames_->setPlainText(state.geometryNames);geometryIndices_->setPlainText(state.geometryIndices);captions_->setPlainText(state.captions);mechanismName_->setText(state.mechanismName);documentTitle_->setText(state.documentTitle);manuscriptTitle_->setText(state.manuscriptTitle);authors_->setText(state.authors);affiliations_->setText(state.affiliations);correspondingEmail_->setText(state.correspondingEmail);computationalDetails_->setPlainText(state.computationalDetails);detailsEdited_=state.computationalDetailsEdited;referencesText_->setPlainText(state.referencesText);figureSettings_=state.figureSettings;preparedFigures_.clear();includeAbsolute_->setChecked(state.includeAbsolute);decimalPlaces_->setValue(state.decimalPlaces);includeDispersion_->setChecked(state.includeDispersion);includeComplexation_->setChecked(state.includeComplexation);baseDirectory_->setText(state.baseDirectory);siOutputPath_->setText(state.siPath);xyzOutputPath_->setText(state.xyzPath);parsePaths(state.files);status_->setText(tr("프로젝트 불러옴: %1").arg(path));
}

bool MainWindow::prepareFigures(){
  if(!validateExport())return false;
  if(documentModel_.coordinates.isEmpty()){QMessageBox::information(this,tr("분자 없음"),tr("먼저 Gaussian 구조를 추가하세요."));return false;}
  for(const auto& block:documentModel_.coordinates)if(block.atoms.empty()){QMessageBox::warning(this,tr("좌표 없음"),block.structureName+tr(": 렌더링할 좌표가 없습니다."));return false;}
  FigureSetupDialog dialog(documentModel_.coordinates,sourcePaths(),figureSettings_,this);
  if(dialog.exec()!=QDialog::Accepted)return false;
  figureSettings_=dialog.settings();preparedFigures_=dialog.figures();refreshDocumentModel();return true;
}
void MainWindow::exportSiDocument(){if(!validateExport())return;
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
  if(renderFigures&&!documentModel_.coordinates.isEmpty()&&!prepareFigures())return;
  auto model=documentModel_;
  if(!renderFigures){model.figures.clear();model.figureCaptions.clear();}
  QString path=siOutputPath_->text().trimmed();if(path.isEmpty())path=QFileDialog::getSaveFileName(this,tr("SI 문서 생성 (Generate SI)"),baseDirectory_->text()+QStringLiteral("/SI.docx"),tr("Word Document (*.docx)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".docx"),Qt::CaseInsensitive))path+=QStringLiteral(".docx");siOutputPath_->setText(path);status_->setText(tr("SI DOCX 생성 중…"));auto* watcher=new QFutureWatcher<QString>(this);connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,path]{const auto error=watcher->result();watcher->deleteLater();if(error.isEmpty())status_->setText(tr("SI 문서 생성 완료: %1").arg(path));else QMessageBox::critical(this,tr("문서 오류 (Document Error)"),error);});watcher->setFuture(QtConcurrent::run([path,model]{QString error;DocxWriter::writeDocument(path,model,&error);return error;}));}
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
