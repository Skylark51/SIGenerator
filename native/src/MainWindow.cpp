#include "MainWindow.hpp"
#include "WindowGeometry.hpp"
#include <QShowEvent>
#include "DocxWriter.hpp"
#include "DocumentPreviewWidget.hpp"
#include "MoleculeViewWidget.hpp"
#include "ProjectStore.hpp"
#include "GaussianSnapshot.hpp"
#include "ProjectManagerDialog.hpp"
#include "StructureAssignmentTable.hpp"
#include <QCloseEvent>
#include "FigureSetupDialog.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include "si/SIModel.hpp"
#include "si/StructureLabels.hpp"
#include "si/XyzWriter.hpp"

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
#include <QTabWidget>
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
QStringList lines(const QString& text){return text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),Qt::SkipEmptyParts);}
QVector<QVector<int>> indexGroups(const QString& text){QVector<QVector<int>> groups;for(const auto& raw:text.split(QRegularExpression(QStringLiteral("[\\r\\n|]+")),Qt::SkipEmptyParts)){QVector<int> group;for(const auto& token:raw.split(QRegularExpression(QStringLiteral("[^0-9]+")),Qt::SkipEmptyParts))group.append(token.toInt());if(!group.isEmpty())groups.append(group);}return groups;}
QVector<QStringList> valueRows(const QString& text){QVector<QStringList> rows;for(const auto& line:lines(text)){auto values=line.trimmed().split(QRegularExpression(QStringLiteral("[\\t ]+")),Qt::SkipEmptyParts);rows.append(values);}return rows;}
QString number(const std::optional<double>& value,int digits,int precision){return si::formatNumber(value,digits,precision);}
QString manualNumber(const QString& value,int){bool ok=false;const double parsed=value.toDouble(&ok);return ok?si::formatNumber(parsed,2,0):value;}
void filterRelativeTable(si::DocumentTable& table,bool dispersion,bool complexation){const auto columns=si::relativeEnergyColumns(dispersion,complexation);QStringList headers;QVector<QStringList> rows;for(int logical:columns)headers.append(table.headers[logical+1]);for(const auto& source:table.rows){QStringList row{source.value(0)};for(int logical:columns)row.append(source.value(logical+1));rows.append(row);}table.headers=QStringList{QString()}+headers;table.rows=rows;table.boldColumns.clear();for(int i=0;i<table.headers.size();++i)if(table.headers[i]==QStringLiteral("E_{Total}")||table.headers[i]==QStringLiteral("ΔG^{c}"))table.boldColumns.append(i);}
QString pairedSinglePointPath(const QString& frequencyPath){return QString::fromStdWString(si::pairedSinglePointPath(std::filesystem::path(frequencyPath.toStdWString())).wstring());}
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
QString atomLabel(const si::GaussianResult& result,int atomId){if(atomId>0&&static_cast<size_t>(atomId)<=result.finalGeometry.size())return QString::fromStdString(result.finalGeometry[static_cast<size_t>(atomId-1)].symbol)+QStringLiteral("(")+QString::number(atomId)+QStringLiteral(")");return QStringLiteral("At(%1)").arg(atomId);}
QGroupBox* section(const QString& title,QWidget* parent,bool collapsible=false){auto* box=new QGroupBox(QString(title).replace("&","&&"),parent);if(collapsible){box->setCheckable(true);box->setChecked(false);QObject::connect(box,&QGroupBox::toggled,box,[box](bool open){for(auto* child:box->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))child->setVisible(open);});}return box;}
}

MainWindow::MainWindow(){
  setObjectName(QStringLiteral("mainWindow"));setAcceptDrops(true);setWindowTitle(tr("SI Generator — 네이티브 (Native)"));resize(1480,900);
  auto* fileMenu=menuBar()->addMenu(tr("파일 (File)"));fileMenu->addAction(tr("Gaussian 파일 열기 (Open Files)…"),QKeySequence::Open,this,&MainWindow::chooseFiles);fileMenu->addAction(tr("고급 · 단계 일괄 구성…"),this,&MainWindow::editReactionStages);fileMenu->addAction(tr("프로젝트 관리…"),this,&MainWindow::projectManagerRequested);fileMenu->addAction(tr("프로젝트 열기 (Open Project)…"),this,&MainWindow::openProject);fileMenu->addAction(tr("프로젝트 저장 (Save Project)…"),QKeySequence::Save,this,&MainWindow::saveProject);
  auto* splitter=new QSplitter(Qt::Horizontal,this);mainSplitter_=splitter;splitter->setObjectName(QStringLiteral("mainSplitter"));splitter->addWidget(createStageNavigation());splitter->addWidget(createInputPanel());splitter->addWidget(createWorkspace());splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setStretchFactor(2,1);splitter->setSizes({210,635,635});
  auto* workspaceScroll=new QScrollArea(this);workspaceScroll->setObjectName("workspaceScroll");workspaceScroll->setWidgetResizable(true);workspaceScroll->setFrameShape(QFrame::NoFrame);workspaceScroll->setWidget(splitter);setCentralWidget(workspaceScroll);createQuickToolbar();
  previewTimer_=new QTimer(this);previewTimer_->setSingleShot(true);previewTimer_->setInterval(220);connect(previewTimer_,&QTimer::timeout,this,&MainWindow::refreshDocumentModel);connectPreviewInputs();
  auto settings=windowSettings();baseDirectory_->setText(settings.value(QStringLiteral("baseDirectory")).toString());restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
  {QSignalBlocker a(referencesText_),b(computationalDetails_);referencesText_->setPlainText(settings.value("documentDraft/referencesText").toString());
   detailsEdited_=settings.value("documentDraft/detailsEdited",false).toBool();if(detailsEdited_)computationalDetails_->setPlainText(settings.value("documentDraft/computationalDetails").toString());}
  connect(referencesText_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();const auto value=referencesText_->toPlainText();draft.setValue("documentDraft/referencesText",value);if(!value.trimmed().isEmpty())draft.setValue("documentDraft/lastNonemptyReferences",value);});
  connect(computationalDetails_,&QTextEdit::textChanged,this,[this]{auto draft=windowSettings();draft.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());draft.setValue("documentDraft/detailsEdited",detailsEdited_);});
  scheduleDocumentRefresh();
}

MainWindow::~MainWindow(){if(importWatcher_)importWatcher_->cancel();auto settings=windowSettings();settings.setValue(QStringLiteral("baseDirectory"),baseDirectory_->text());settings.setValue(QStringLiteral("geometry"),saveGeometry());settings.setValue("documentDraft/referencesText",referencesText_->toPlainText());settings.setValue("documentDraft/computationalDetails",computationalDetails_->toPlainText());settings.setValue("documentDraft/detailsEdited",detailsEdited_);}

void MainWindow::showEvent(QShowEvent* event){
  QMainWindow::showEvent(event);
  QTimer::singleShot(0,this,[this]{fitWindowToScreen(this);});
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
  spinSection_=section(tr("Mulliken 스핀 밀도 (Spin Density)"),content,true);auto* spin=new QFormLayout(spinSection_);spinNames_=new QTextEdit(spinSection_);spinNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));spinNames_->setFixedHeight(45);spin->addRow(spinNames_);spinIndices_=new QTextEdit(spinSection_);spinIndices_->setPlaceholderText(tr("원자 ID — 비우면 절댓값 상위 6개 자동 선택 (Atom IDs)"));spinIndices_->setFixedHeight(45);spin->addRow(spinIndices_);layout->addWidget(spinSection_);
  geometrySection_=section(tr("선택 구조 (Selected Geometry)"),content,true);auto* geometry=new QFormLayout(geometrySection_);geometryNames_=new QTextEdit(geometrySection_);geometryNames_->setPlaceholderText(tr("구조명 — 비우면 구조 목록 사용 (Structure Labels)"));geometryNames_->setFixedHeight(45);geometry->addRow(geometryNames_);geometryIndices_=new QTextEdit(geometrySection_);geometryIndices_->setPlaceholderText(tr("2/3/4개 원자 ID: 거리/각/이면각 — 비우면 RMSD 자동 분석"));geometryIndices_->setFixedHeight(55);geometry->addRow(geometryIndices_);geometryAutoInfo_=new QLabel(geometrySection_);geometryAutoInfo_->setWordWrap(true);geometryAutoInfo_->setObjectName("geometryAutoInfo");geometry->addRow(geometryAutoInfo_);auto* addGeometry=new QPushButton(tr("클릭 선택 추가 (Add Click Selection)"),geometrySection_);connect(addGeometry,&QPushButton::clicked,this,&MainWindow::appendSelectedGeometry);geometry->addRow(addGeometry);layout->addWidget(geometrySection_);
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
    auto* page=new QWidget(scroll);auto* pageLayout=new QVBoxLayout(page);pageLayout->setContentsMargins(10,6,10,10);if(group!=filesSection_){auto* scope=new QLabel(tr("SI 전체 공통 설정 · 반응별 구조·기준·표는 구조 화면에서 설정합니다."),page);scope->setWordWrap(true);pageLayout->addWidget(scope);}pageLayout->addWidget(group);pageLayout->addStretch();scroll->setWidget(page);inputPages_->addWidget(scroll);
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
  generateSi->setText(tr("SI 생성"));generateSi->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);generateSi->setFixedWidth(110);
  add("generateCoordinatesButton",tr("전체 구조 좌표 DOCX 생성"),navigationIcon("coordinates",QColor("#087e8b")),[this]{exportCoordinatesDocument();});
  add("generateXyzButton",tr("전체 구조 XYZ 파일 생성"),navigationIcon("xyz",QColor("#087e8b")),[this]{exportXyzDocument();});
  auto* currentSection=new QLabel(labels[0],bar);currentSection->setObjectName("currentSectionLabel");bar->addWidget(currentSection);
  connect(inputPages_,&QStackedWidget::currentChanged,this,[this,currentSection,labels](int index){inputScroll_=qobject_cast<QScrollArea*>(inputPages_->widget(index));navigation_->button(index)->setChecked(true);currentSection->setText(labels[index]);});
  return bar;
}

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
  refreshStageNavigation();refreshStageWorkspace();syncReferenceOptions();
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
        if(item.pairPath.isEmpty())item.pairPath=pairedSinglePointPath(path);
        if(!item.pairPath.isEmpty())item.pair=parse(item.pairPath);
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
std::vector<si::EnergyRecord> MainWindow::energyRecords() const{
  std::vector<si::EnergyRecord> records;
  for(int row=0;row<files_->rowCount();++row){
    const auto* item=files_->item(row,0);if(!item)continue;
    const auto path=item->data(Qt::UserRole).toString();const auto main=parsed_.value(path);if(!main)continue;
    si::EnergyRecord record;record.name=item->text().toStdString();record.frequency=*main;
    if(const auto pair=parsed_.value(pairedPaths_.value(path));pair&&pair->success){record.singlePoint=*pair;if(record.frequency.finalGeometry.empty()&&!pair->finalGeometry.empty())record.frequency.finalGeometry=pair->finalGeometry;}
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
  updateStructureStatusCells();
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
void MainWindow::renameSelected(){const int row=files_->currentRow();if(row<0)return;QString name=structureLabel_->text().trimmed();if(name.isEmpty()){bool accepted=false;name=QInputDialog::getText(this,tr("구조 이름 (Structure Label)"),tr("구조 이름 (Structure Label):"),QLineEdit::Normal,files_->item(row,0)->text(),&accepted).trimmed();if(!accepted||name.isEmpty())return;}files_->item(row,0)->setText(name);manualLabelsEdited_=true;labelInputAtManualEdit_=fileNames_->toPlainText();labelBaseAtManualEdit_=baseDirectory_->text();syncReferenceOptions();scheduleDocumentRefresh();}
void MainWindow::removeSelected(){const int row=files_->currentRow();if(row<0)return;const auto path=files_->item(row,0)->data(Qt::UserRole).toString();for(int i=0;i<reactionStages_.size();++i){auto stage=reactionStages_[i].toObject();auto members=stage["files"].toArray();for(int j=members.size()-1;j>=0;--j)if(members[j].toString()==path)members.removeAt(j);stage["files"]=members;if(stage["reference"].toString()==path)stage["reference"]=members.isEmpty()?QString():members.first().toString();reactionStages_[i]=stage;}files_->removeRow(row);parsed_.remove(path);if(files_->rowCount())files_->selectRow(std::min(row,files_->rowCount()-1));else moleculeView_->clearMolecule();syncReferenceOptions();scheduleDocumentRefresh();}
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
  applyReactionStages(model);for(int i=0;i<model.tables.size();++i)model.tables[i].documentNumber=i+1;return model;
}

void MainWindow::scheduleDocumentRefresh(){if(previewTimer_)previewTimer_->start();}
void MainWindow::refreshDocumentModel(){if(parsing_)return;refreshPreflight();refreshStageNavigation();documentModel_=buildDocumentModel();refreshStageWorkspace();refreshScopedPreview();}
void MainWindow::updateGeometryFeedback(const QVector<int>& ids){if(ids.isEmpty()){interactionInfo_->setText(tr("원자 2/3/4개를 순서대로 선택하세요. (Select 2/3/4 atoms.)"));return;}const int row=files_->currentRow();if(row<0)return;const auto result=parsed_.value(files_->item(row,0)->data(Qt::UserRole).toString());if(!result)return;QStringList labels;for(const int id:ids)labels.append(atomLabel(*result,id));QString value;if(ids.size()==2){const auto calculated=si::distance(result->finalGeometry,ids[0],ids[1]);if(calculated)value=QStringLiteral(" = %1 Å").arg(*calculated,0,'f',3);}else if(ids.size()==3){const auto calculated=si::angle(result->finalGeometry,ids[0],ids[1],ids[2]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}else if(ids.size()==4){const auto calculated=si::dihedral(result->finalGeometry,ids[0],ids[1],ids[2],ids[3]);if(calculated)value=QStringLiteral(" = %1°").arg(*calculated,0,'f',3);}interactionInfo_->setText(labels.join(QStringLiteral(" – "))+value);}
void MainWindow::appendSelectedGeometry(){const auto ids=moleculeView_->selectedAtoms();if(ids.size()<2||ids.size()>4){QMessageBox::information(this,tr("선택 구조 (Selected Geometry)"),tr("원자 2개(거리), 3개(각), 또는 4개(이면각)를 선택하세요."));return;}QStringList tokens;for(const int id:ids)tokens.append(QString::number(id));auto text=geometryIndices_->toPlainText().trimmed();if(!text.isEmpty())text+=QChar('\n');geometryIndices_->setPlainText(text+tokens.join(' '));if(geometryNames_->toPlainText().trimmed().isEmpty())geometryNames_->setPlainText(structureLabels().join('\n'));moleculeView_->clearSelection();scheduleDocumentRefresh();}
void MainWindow::scrollTo(QWidget* target){
  for(int i=0;i<inputPages_->count();++i)if(inputPages_->widget(i)->isAncestorOf(target)){inputPages_->setCurrentIndex(i);break;}
  if(auto* box=qobject_cast<QGroupBox*>(target);box&&box->isCheckable())box->setChecked(true);
  if(target->parentWidget()!=inputScroll_->widget())inputScroll_->ensureWidgetVisible(target,10,10);
}

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
  refreshDocumentModel();ProjectState state;state.projectName=projectName_;state.files=sourcePaths();state.structureNames=structureLabels().join('\n');state.referenceMode=referenceMode_->currentData().toString();state.referencePath=reference_->currentData().toString();state.manualReference=manualReference_->text();state.energyInputMode=energyInputMode_->currentData().toString();state.energyBasis=QString::number(energyBasis_->currentData().toInt());state.manualAbsoluteNames=manualAbsoluteNames_->toPlainText();state.manualAbsoluteValues=manualAbsoluteValues_->toPlainText();state.manualRelativeNames=manualRelativeNames_->toPlainText();state.manualRelativeValues=manualRelativeValues_->toPlainText();state.spinNames=spinNames_->toPlainText();state.spinIndices=spinIndices_->toPlainText();state.geometryNames=geometryNames_->toPlainText();state.geometryIndices=geometryIndices_->toPlainText();state.captions=captions_->toPlainText();state.mechanismName=mechanismName_->text();state.documentTitle=documentTitle_->text();state.manuscriptTitle=manuscriptTitle_->text();state.authors=authors_->text();state.affiliations=affiliations_->text();state.correspondingEmail=correspondingEmail_->text();state.computationalDetails=computationalDetails_->toPlainText();state.computationalDetailsEdited=detailsEdited_;state.referencesText=referencesText_->toPlainText();state.figureSettings=figureSettings_;state.reactionStages=reactionStages_;state.includeAbsolute=includeAbsolute_->isChecked();state.decimalPlaces=0;state.includeDispersion=includeDispersion_->isChecked();state.includeComplexation=includeComplexation_->isChecked();state.baseDirectory=baseDirectory_->text();state.siPath=siOutputPath_->text();state.xyzPath=xyzOutputPath_->text();
  for(const auto& source:state.files){
    if(const auto result=parsed_.value(source))
      state.gaussianSnapshots.insert(source,si::GaussianSnapshot::encode(*result));
    const auto pairedPath=pairedPaths_.value(source);
    if(!pairedPath.isEmpty()){
      if(const auto paired=parsed_.value(pairedPath)){
        state.pairedSnapshotPaths.insert(source,pairedPath);
        state.gaussianSnapshots.insert(pairedPath,si::GaussianSnapshot::encode(*paired));
      }
    }
  }
  QString error;if(!ProjectStore::save(path,state,&error)){QMessageBox::critical(this,tr("저장 실패 (Save Failed)"),error);return false;}currentProjectPath_=path;ProjectManagerDialog::rememberProject(path);status_->setText(tr("프로젝트 저장됨: %1").arg(path));return true;}
void MainWindow::openProject(){
  const auto path=QFileDialog::getOpenFileName(this,tr("SI 프로젝트 열기 (Open Project)"),baseDirectory_->text(),tr("SI Project (*.si_project.json *.json)"));if(path.isEmpty())return;
  loadProjectPath(path);
}
bool MainWindow::loadProjectPath(const QString& path){
  ProjectState state;QString error;if(!ProjectStore::load(path,&state,&error)){QMessageBox::critical(this,tr("열기 실패 (Open Failed)"),error);return false;}
  if(!currentProjectPath_.isEmpty()&&!saveActiveProject())return false;
  pendingStructureLabels_=lines(state.structureNames);pendingLabelsByPath_.clear();for(int i=0;i<state.files.size();++i)pendingLabelsByPath_.insert(QFileInfo(state.files[i]).absoluteFilePath(),pendingStructureLabels_.value(i,QFileInfo(state.files[i]).completeBaseName()));
  referenceMode_->setCurrentIndex(std::max(0,referenceMode_->findData(state.referenceMode)));pendingReferencePath_=state.referencePath;manualReference_->setText(state.manualReference);energyInputMode_->setCurrentIndex(std::max(0,energyInputMode_->findData(state.energyInputMode)));bool ok=false;const int basis=state.energyBasis.toInt(&ok);if(ok&&basis==static_cast<int>(si::EnergyBasis::LegacyComposite)&&energyBasis_->findData(basis)<0)energyBasis_->addItem("Legacy composite (saved project)",basis);if(ok)energyBasis_->setCurrentIndex(std::max(0,energyBasis_->findData(basis)));manualAbsoluteNames_->setPlainText(state.manualAbsoluteNames);manualAbsoluteValues_->setPlainText(state.manualAbsoluteValues);manualRelativeNames_->setPlainText(state.manualRelativeNames);manualRelativeValues_->setPlainText(state.manualRelativeValues);spinNames_->setPlainText(state.spinNames);spinIndices_->setPlainText(state.spinIndices);geometryNames_->setPlainText(state.geometryNames);geometryIndices_->setPlainText(state.geometryIndices);captions_->setPlainText(state.captions);mechanismName_->setText(state.mechanismName);documentTitle_->setText(state.documentTitle);manuscriptTitle_->setText(state.manuscriptTitle);authors_->setText(state.authors);affiliations_->setText(state.affiliations);correspondingEmail_->setText(state.correspondingEmail);computationalDetails_->setPlainText(state.computationalDetails);detailsEdited_=state.computationalDetailsEdited;referencesText_->setPlainText(state.referencesText);figureSettings_=state.figureSettings;reactionStages_=state.reactionStages;preparedFigures_.clear();preparedStageFigures_.clear();includeAbsolute_->setChecked(state.includeAbsolute);decimalPlaces_->setValue(0);includeDispersion_->setChecked(state.includeDispersion);includeComplexation_->setChecked(state.includeComplexation);const QDir projectDirectory(QFileInfo(path).absolutePath());
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
  activeStage_=reactionStages_.isEmpty()?-2:0;parsePaths(state.files,false,false,QStringLiteral("@project-restore"),state.gaussianSnapshots,state.pairedSnapshotPaths);refreshStageNavigation();refreshStageWorkspace();scrollTo(filesSection_);status_->setText(tr("프로젝트: %1 · 전환 및 종료 시 자동 저장").arg(projectName_));return true;
}

bool MainWindow::prepareFigures(){
  if(!validateExport())return false;
  if(activeStage_<0&&!reactionStages_.isEmpty()){const int previous=activeStage_;for(int i=0;i<reactionStages_.size();++i){if(reactionStages_[i].toObject()["files"].toArray().isEmpty())continue;activeStage_=i;if(!prepareFigures()){activeStage_=previous;refreshStageWorkspace();return false;}}activeStage_=previous;refreshStageWorkspace();refreshScopedPreview();return true;}
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
void MainWindow::exportSiDocument(){applyNamedLabelsForExport();if(!validateExport())return;
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
    if(reactionStages_.isEmpty()){if(!prepareFigures())return;}
    else {const int previous=activeStage_;for(int i=0;i<reactionStages_.size();++i){const auto stage=reactionStages_[i].toObject();if(stage["files"].toArray().isEmpty())continue;const auto id=stage.value("id").toString(stage["name"].toString());if(preparedStageFigures_.contains(id))continue;activeStage_=i;if(!prepareFigures()){activeStage_=previous;refreshStageWorkspace();refreshScopedPreview();return;}}activeStage_=previous;refreshStageWorkspace();refreshDocumentModel();}
  }
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

void MainWindow::editReactionStages(){
  if(parsing_){status_->setText(tr("파일 해석이 끝난 뒤 반응 단계를 구성하세요."));return;}
  QDialog dialog(this);dialog.setObjectName("reactionStageDesigner");dialog.setWindowTitle(tr("SI 프로젝트 — 하위 반응 단계와 표 구성"));dialog.resize(1050,800);
  auto* layout=new QVBoxLayout(&dialog);
  auto* note=new QLabel(tr("구조명은 셀을 더블클릭하거나 아래 편집칸에서 직접 수정할 수 있습니다. 변경한 이름은 SI 전체에 반영됩니다.\n엑셀의 구조명·Output 두 열을 복사한 뒤 표에서 Ctrl+V로 붙여넣으세요. 파일은 불러온 구조 목록에서 연결합니다.\n체크한 구조만 해당 단계의 표에 포함됩니다. 단계는 드래그하거나 위로·아래로 이동할 수 있습니다."),&dialog);note->setWordWrap(true);layout->addWidget(note);
  auto* columns=new QHBoxLayout;layout->addLayout(columns,1);
  auto* stages=new QListWidget(&dialog);stages->setObjectName("reactionStageList");stages->setDragDropMode(QAbstractItemView::InternalMove);stages->setDefaultDropAction(Qt::MoveAction);stages->setSpacing(6);stages->setStyleSheet("QListWidget::item {padding:14px 10px;border:1px solid #b8cce2;border-radius:7px;background:#edf4fc;} QListWidget::item:selected {background:#d1e5fc;color:#163b67;border:2px solid #397ec1;}");columns->addWidget(stages,1);
  auto* form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);columns->addLayout(form,3);
  auto* name=new QLineEdit(&dialog);name->setObjectName("reactionStageName");form->addRow(tr("반응 단계 이름"),name);
  auto* members=new StructureAssignmentTable(&dialog);form->addRow(tr("구조 목록"),members);
  auto* paste=new QPushButton(tr("엑셀 두 열 붙여넣기 (Ctrl+V)"),&dialog);paste->setObjectName("pasteStageStructures");form->addRow(paste);
  auto* feedback=new QLabel(&dialog);feedback->setObjectName("stagePasteFeedback");feedback->setWordWrap(true);form->addRow(feedback);
  auto* labelEdit=new QLineEdit(&dialog);labelEdit->setObjectName("stageStructureNameEdit");form->addRow(tr("선택 구조명"),labelEdit);
  auto* formatting=new QHBoxLayout;auto* superscript=new QPushButton(tr("위첨자 x²"),&dialog);superscript->setObjectName("stageSuperscriptButton");auto* subscript=new QPushButton(tr("아래첨자 x₂"),&dialog);subscript->setObjectName("stageSubscriptButton");
  for(auto* button:{superscript,subscript}){button->setFocusPolicy(Qt::NoFocus);formatting->addWidget(button);}
  auto* labelPreview=new QLabel(&dialog);labelPreview->setObjectName("stageStructureNamePreview");labelPreview->setTextFormat(Qt::RichText);formatting->addWidget(labelPreview,1);form->addRow(formatting);
  auto* hint=new QLabel(tr("글자를 선택한 뒤 첨자 버튼을 누르세요. 직접 입력도 가능: ^{5}D_{RC}-TS · 5DRC-TS는 자동 첨자"),&dialog);hint->setWordWrap(true);form->addRow(hint);
  auto* reference=new QComboBox(&dialog);reference->setObjectName("reactionStageReference");reference->setItemDelegate(new StructureNameDelegate(reference));form->addRow(tr("단계 기준 구조"),reference);
  const QStringList keys={"absolute","relative","spin","geometry"};const QStringList titles={tr("절대 에너지 (에너지 화면의 포함 설정 적용)"),tr("상대 에너지"),tr("스핀 밀도"),tr("선택 구조")};
  QList<QCheckBox*> checks;for(const auto& title:titles){auto* check=new QCheckBox(title,&dialog);check->setChecked(true);form->addRow(check);checks.append(check);}
  const auto paths=sourcePaths(),labels=structureLabels();QHash<QString,QString> draftLabels;for(int i=0;i<paths.size();++i)draftLabels.insert(paths[i],labels.value(i));bool loading=false;
  auto showSelection=[&]{const int row=members->currentRow();const bool valid=row>=0&&members->isEnabled();const auto text=valid?members->item(row,0)->text():QString();if(labelEdit->text()!=text){QSignalBlocker blocked(labelEdit);labelEdit->setText(text);}labelEdit->setEnabled(valid);superscript->setEnabled(valid);subscript->setEnabled(valid);labelPreview->setText(si::formattedTextHtml(si::structureLabelMarkup(text)));};
  auto updateReference=[&]{const auto previous=reference->currentData().toString();reference->clear();for(int i=0;i<members->rowCount();++i)if(members->item(i,0)->checkState()==Qt::Checked)reference->addItem(members->item(i,0)->text(),members->item(i,0)->data(Qt::UserRole));reference->setCurrentIndex(std::max(0,reference->findData(previous)));};
  auto save=[&](QListWidgetItem* item){if(!item)return;QJsonObject stage=item->data(Qt::UserRole).toJsonObject();stage["name"]=name->text();QJsonArray files;
    for(int i=0;i<members->rowCount();++i)if(members->item(i,0)->checkState()==Qt::Checked)files.append(members->item(i,0)->data(Qt::UserRole).toString());stage["files"]=files;stage["reference"]=reference->currentData().toString();for(int i=0;i<keys.size();++i)stage[keys[i]]=checks[i]->isChecked();item->setData(Qt::UserRole,stage);item->setText(stage["name"].toString());};
  auto load=[&](QListWidgetItem* item){loading=true;members->setRowCount(0);reference->clear();const auto stage=item?item->data(Qt::UserRole).toJsonObject():QJsonObject();name->setText(stage["name"].toString());const auto files=stage["files"].toArray();
    members->setRowCount(paths.size());for(int i=0;i<paths.size();++i){auto* label=new QTableWidgetItem(draftLabels.value(paths[i]));label->setData(Qt::UserRole,paths[i]);label->setFlags(label->flags()|Qt::ItemIsUserCheckable);label->setCheckState(files.contains(paths[i])?Qt::Checked:Qt::Unchecked);members->setItem(i,0,label);auto* output=new QTableWidgetItem(QFileInfo(paths[i]).fileName());output->setFlags(output->flags()&~Qt::ItemIsEditable);output->setToolTip(QDir::toNativeSeparators(paths[i]));members->setItem(i,1,output);}
    name->setEnabled(item);members->setEnabled(item);paste->setEnabled(item);reference->setEnabled(item);for(auto* check:checks)check->setEnabled(item);feedback->clear();updateReference();reference->setCurrentIndex(std::max(0,reference->findData(stage["reference"].toString())));for(int i=0;i<keys.size();++i)checks[i]->setChecked(stage.value(keys[i]).toBool(true));if(!paths.isEmpty())members->setCurrentCell(0,0);loading=false;showSelection();};
  connect(members,&QTableWidget::currentCellChanged,&dialog,[&]{if(!loading)showSelection();});
  connect(members,&QTableWidget::itemChanged,&dialog,[&](QTableWidgetItem* item){if(loading||item->column()!=0)return;draftLabels[item->data(Qt::UserRole).toString()]=item->text();updateReference();showSelection();});
  connect(labelEdit,&QLineEdit::textChanged,&dialog,[&](const QString& text){if(!loading&&members->currentRow()>=0)members->item(members->currentRow(),0)->setText(text);});
  auto format=[&](const QString& marker){if(labelEdit->selectionStart()<0){feedback->setText(tr("선택 구조명에서 첨자로 만들 글자를 먼저 선택하세요."));labelEdit->setFocus();return;}const int start=labelEdit->selectionStart(),length=labelEdit->selectedText().size();auto text=labelEdit->text();text.replace(start,length,marker+"{"+labelEdit->selectedText()+"}");labelEdit->setText(text);labelEdit->setFocus();labelEdit->setSelection(start+2,length);};
  connect(superscript,&QPushButton::clicked,&dialog,[&]{format("^");});connect(subscript,&QPushButton::clicked,&dialog,[&]{format("_");});
  members->pasteText=[&](const QString& text){if(!members->isEnabled())return;int changed=0,nextRow=std::max(0,members->currentRow());QStringList problems;
    auto findRow=[&](QString file){file=file.trimmed();if(file.size()>1&&file.startsWith('"')&&file.endsWith('"'))file=file.mid(1,file.size()-2);file=QDir::fromNativeSeparators(file);QList<int> matches;const bool explicitPath=file.contains('/')||QFileInfo(file).isAbsolute();const auto absolute=QFileInfo(QDir(baseDirectory_->text()).filePath(file)).absoluteFilePath();for(int i=0;i<paths.size();++i){const QFileInfo info(paths[i]);const bool match=explicitPath?info.absoluteFilePath().compare(absolute,Qt::CaseInsensitive)==0:(info.fileName().compare(file,Qt::CaseInsensitive)==0||info.completeBaseName().compare(file,Qt::CaseInsensitive)==0);if(match)matches.append(i);}return matches.size()==1?matches[0]:matches.isEmpty()?-1:-2;};
    auto apply=[&](const QString& label,const QString& file){const int row=findRow(file);if(row<0){problems.append(row==-2?tr("동일 파일명이 여러 개입니다. 전체 경로를 지정하세요: %1").arg(file):tr("불러온 Output 없음: %1 (구조와 파일 화면에서 먼저 추가하세요)").arg(file));return;}auto* item=members->item(row,0);if(!label.trimmed().isEmpty())item->setText(label.trimmed());item->setCheckState(Qt::Checked);++changed;};
    for(const auto& line:text.split(QRegularExpression("[\\r\\n]+"),Qt::SkipEmptyParts)){const auto cells=line.split('\t');if(cells.size()==2){if((cells[0].contains(tr("구조명"))||cells[0].contains(tr("라벨링"))||cells[0].compare("Structure",Qt::CaseInsensitive)==0)&&(cells[1].contains("Output",Qt::CaseInsensitive)||cells[1].contains(tr("파일"))))continue;apply(cells[0],cells[1]);continue;}if(cells.size()>2){problems.append(tr("구조명과 Output의 두 열만 복사하세요."));continue;}
      const auto entries=si::namedStructureInputs(line);if(entries.size()==1&&!entries[0].label.isEmpty()){apply(entries[0].label,entries[0].fileName);continue;}const int fileRow=findRow(line);if(fileRow>=0){apply({},line);continue;}if(line.endsWith(".out",Qt::CaseInsensitive)||line.endsWith(".log",Qt::CaseInsensitive)){apply({},line);continue;}if(nextRow>=members->rowCount()){problems.append(tr("구조명 입력이 목록의 행 수를 초과했습니다."));break;}members->item(nextRow++,0)->setText(line.trimmed());++changed;
    }
    feedback->setText(tr("%1개 행 적용").arg(changed)+(problems.isEmpty()?QString():"\n"+problems.mid(0,4).join('\n')));};
  connect(paste,&QPushButton::clicked,&dialog,[&]{members->pasteText(QApplication::clipboard()->text());});
  connect(stages,&QListWidget::currentItemChanged,&dialog,[&](QListWidgetItem* current,QListWidgetItem* previous){save(previous);load(current);});
  for(const auto& value:reactionStages_){auto* item=new QListWidgetItem(value.toObject()["name"].toString(),stages);item->setData(Qt::UserRole,value.toObject());}
  auto* actions=new QHBoxLayout;layout->addLayout(actions);auto* add=new QPushButton(tr("반응 단계 추가"),&dialog);auto* remove=new QPushButton(tr("선택 단계 제거"),&dialog);actions->addWidget(add);actions->addWidget(remove);actions->addStretch();
  connect(add,&QPushButton::clicked,&dialog,[&]{auto* item=new QListWidgetItem(tr("반응 단계 %1").arg(stages->count()+1),stages);item->setData(Qt::UserRole,QJsonObject{{"name",item->text()},{"files",QJsonArray()}});stages->setCurrentItem(item);});
  connect(remove,&QPushButton::clicked,&dialog,[&]{const int row=stages->currentRow();if(row>=0){auto* item=stages->takeItem(row);delete item;}if(!stages->count())load(nullptr);});
  auto* up=new QPushButton(tr("↑ 위로"),&dialog);up->setObjectName("stageMoveUp");auto* down=new QPushButton(tr("↓ 아래로"),&dialog);down->setObjectName("stageMoveDown");actions->addWidget(up);actions->addWidget(down);
  auto move=[&](int delta){int row=stages->currentRow(),target=row+delta;if(row<0||target<0||target>=stages->count())return;save(stages->currentItem());QSignalBlocker blocked(stages);auto* item=stages->takeItem(row);stages->insertItem(target,item);stages->setCurrentItem(item);};connect(up,&QPushButton::clicked,&dialog,[&]{move(-1);});connect(down,&QPushButton::clicked,&dialog,[&]{move(1);});
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(tr("표 구성 적용"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{for(const auto& label:draftLabels)if(label.trimmed().isEmpty()){feedback->setText(tr("구조명은 비워 둘 수 없습니다."));return;}save(stages->currentItem());QSet<QString> names;for(int i=0;i<stages->count();++i){const auto stage=stages->item(i)->data(Qt::UserRole).toJsonObject();const auto title=stage["name"].toString();if(title.trimmed().isEmpty()||names.contains(title)||stage["files"].toArray().isEmpty()){feedback->setText(tr("각 단계에 서로 다른 이름과 하나 이상의 구조를 지정하세요."));return;}names.insert(title);}dialog.accept();});
  if(stages->count())stages->setCurrentRow(0);else load(nullptr);
  if(dialog.exec()!=QDialog::Accepted)return;
  reactionStages_=QJsonArray();for(int i=0;i<stages->count();++i)reactionStages_.append(stages->item(i)->data(Qt::UserRole).toJsonObject());
  bool renamed=false;{QSignalBlocker blocked(files_);for(int i=0;i<paths.size();++i){const auto label=draftLabels.value(paths[i]).trimmed();renamed|=label!=labels[i];files_->item(i,0)->setText(label);}}
  if(renamed){for(auto* edit:{manualAbsoluteNames_,manualRelativeNames_,spinNames_,geometryNames_}){auto names=lines(edit->toPlainText());for(auto& value:names){const int index=labels.indexOf(value);if(index>=0&&labels.count(value)==1)value=draftLabels.value(paths[index]).trimmed();}QSignalBlocker blocked(edit);edit->setPlainText(names.join('\n'));}manualLabelsEdited_=true;labelInputAtManualEdit_=fileNames_->toPlainText();labelBaseAtManualEdit_=baseDirectory_->text();}
  syncReferenceOptions();if(auto* item=files_->item(files_->currentRow(),0))structureLabel_->setText(item->text());refreshDocumentModel();status_->setText(tr("하위 반응 단계 %1개와 구조명을 적용했습니다. 프로젝트 저장 시 함께 저장됩니다.").arg(reactionStages_.size()));
}
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
    {if(!stage.value(key).toBool(key=="absolute"?includeAbsolute_->isChecked():true))continue;const auto members=stage["files"].toArray();auto split=table;split.stageId=stage.value("id").toString(stage["name"].toString());split.rows.clear();std::vector<si::EnergyRecord> selected;
      for(const auto& member:members){const int i=paths.indexOf(member.toString());if(i<0)continue;if(manualEnergy(table)){for(const auto& row:table.rows)if(row.value(0)==labels[i])split.rows.append(row);}else split.rows.append(table.rows[i]);if(i<static_cast<int>(records.size()))selected.push_back(records[i]);}
      if(split.rows.isEmpty())continue;
      split.caption=stage["name"].toString()+": "+table.caption;
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
        for(const int atomId:commonIds)split.headers.append(atomLabel(selected.front().frequency,atomId));
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
      if(key=="relative"&&(stage.value("manual_relative_enabled").toBool(false)||(sourceStageIndex>=0&&stageNeedsManualRelative(sourceStageIndex)))){
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
        split.caption+=tr(" Reference: %1.").arg(QString::fromStdString(selected[ref].name));
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
        stage["name"].toString()+tr(": Selected Geometries of %1 in Å and °.").arg(si::displayMechanism(model)),
        {QString()},{},{}};
      geometryTable.stageId=id;
      const auto& labelSource=geometryRecords[suggestion.recordIndices.front()].frequency;
      for(const auto& pair:suggestion.distances)
        geometryTable.headers.append(atomLabel(labelSource,pair[0])+"-"+atomLabel(labelSource,pair[1])+QStringLiteral(" (Å)"));
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
