#include "DocumentPreviewWidget.hpp"
#include "MainWindow.hpp"
#include "ProjectManagerDialog.hpp"
#include "StructureAssignmentTable.hpp"
#include <QListWidget>
#include <QGroupBox>
#include "MoleculeViewWidget.hpp"
#include "si/GaussianParser.hpp"
#include <QApplication>
#include <QStackedWidget>
#include <QTabWidget>
#include <QSplitter>
#include <QToolBar>
#include <QStyleFactory>
#include <QClipboard>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QTextTable>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDialog>
#include <QLineEdit>
#include <QInputDialog>
#include <QTextEdit>
#include <QLabel>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include "DocxWriter.hpp"
#include "FigureSetupDialog.hpp"
#include "MolecularStyle.hpp"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include "ProjectStore.hpp"
#include "si/XyzWriter.hpp"
#include "si/StructureLabels.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QMimeData>
#include <QScrollBar>
#include <QScrollArea>
#include <QScreen>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QTimer>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>

namespace {
bool waitUntil(const std::function<bool()>& fn,int ms=5000){QElapsedTimer t;t.start();while(!fn()&&t.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,50);QTest::qWait(20);}return fn();}
bool check(bool value,const char* message){if(!value)std::cerr<<"FAILED: "<<message<<'\n';return value;}
void chooseSiRendering(QWidget* window,QMessageBox::ButtonRole role){
  QTimer::singleShot(0,window,[window,role]{
    auto* choice=window->findChild<QMessageBox*>("siRenderingChoice");
    if(!choice)return;
    if(role==QMessageBox::YesRole)QTimer::singleShot(0,window,[window]{
      if(auto* dialog=window->findChild<FigureSetupDialog*>())dialog->findChild<QPushButton*>("nextFigureGroup")->click();
    });
    for(auto* button:choice->buttons())if(choice->buttonRole(button)==role){button->click();break;}
  });
}
void clickClipboardButton(QPushButton* button){
  // Allow Windows clipboard consumers to release the clipboard between copies.
  QTest::qWait(80);button->click();QTest::qWait(80);
}
bool tableImageSmoke(){
  DocumentPreviewWidget preview;
  si::SIDocumentModel model;
  si::DocumentTable table;
  table.section=si::DocumentSection::RelativeEnergy;
  table.headers={"Structure","ΔΔE^{a}","ΔG^{c}"};
  table.rows={{"^{1}TS_{E}","+1.23","-4.56"}};
  table.boldColumns={2};table.caption="Caption must not be in image";
  model.tables.append(table);preview.setDocumentModel(model);
  auto* selector=preview.findChild<QComboBox*>("previewTableSelector");
  auto* copy=preview.findChild<QPushButton*>("copyPreviewTableImageButton");
  if(!selector||!copy)return check(false,"image copy controls exist");
  selector->setCurrentIndex(1);clickClipboardButton(copy);
  const auto readImage=[]{return QImage::fromData(QApplication::clipboard()->mimeData()->data("image/png"),"PNG");};
  const QImage shortImage=readImage();
  bool ok=check(!shortImage.isNull(),"formatted table image rendered");
  std::vector<int> ruleWidths;
  bool inRule=false;
  for(int y=0;y<shortImage.height();++y){
    int opaque=0;
    for(int x=0;x<shortImage.width();++x)if(qAlpha(shortImage.pixel(x,y))>200)++opaque;
    const bool rule=opaque>shortImage.width()*0.85;
    if(rule){if(!inRule)ruleWidths.push_back(0);++ruleWidths.back();}
    inRule=rule;
  }
  ok&=check(ruleWidths.size()==3,"table image has top, header, and bottom horizontal rules");
  if(ruleWidths.size()==3)ok&=check(ruleWidths[0]==ruleWidths[1]&&ruleWidths[1]==ruleWidths[2],"all table image rules have equal thickness");
  auto* copyTable=preview.findChild<QPushButton*>("copyPreviewTableButton");
  if(copyTable){
    clickClipboardButton(copyTable);
    const auto html=QApplication::clipboard()->mimeData()->html();
    ok&=check(html.count("border-top:0.75pt solid black")==3&&html.count("border-bottom:0.75pt solid black")==6,"clipboard HTML applies uniform rules directly to header and final row cells");
  }
  model.tables[0].caption=QString(2000,'X');preview.setDocumentModel(model);clickClipboardButton(copy);
  ok&=check(readImage()==shortImage,"image excludes caption and its layout");
  preview.resize(160,80);
  for(int i=0;i<100;++i)model.tables[0].rows.append(table.rows.front());
  preview.setDocumentModel(model);clickClipboardButton(copy);
  const QImage tallImage=readImage();
  ok&=check(tallImage.width()==shortImage.width()&&tallImage.height()>shortImage.height()*20,"image includes all rows even with tiny preview viewport");
  if(qEnvironmentVariableIsSet("SI_TABLE_IMAGE_SAMPLE"))shortImage.save(qEnvironmentVariable("SI_TABLE_IMAGE_SAMPLE"));
  preview.setDocumentModel(si::SIDocumentModel{});
  ok&=check(!copy->isEnabled(),"image copy disabled after tables removed");
  return ok;
}
bool singleLineTableSmoke(){
  DocumentPreviewWidget preview;preview.resize(260,180);preview.show();
  si::SIDocumentModel model;
  si::DocumentTable table;table.headers={"Structure","E_{Def2-SVP}","E_{Def2-TZVPP}","ΔΔE^{a}","ΔΔE_{Thermal}^{b}","-TΔS^{b}","Δ_{Dispersion}","ΔG^{c}"};
  table.rows={{"^{1}TS_{E}","-123.45678900","-123.45678900","+1.23","+2.34","-3.45","-0.01","+0.12"}};
  table.boldColumns={7};
  model.tables.append(table);preview.setDocumentModel(model);
  auto* selector=preview.findChild<QComboBox*>("previewTableSelector");
  auto* imageButton=preview.findChild<QPushButton*>("copyPreviewTableImageButton");
  auto* browser=preview.findChild<QTextBrowser*>();
  if(!selector||!imageButton||!browser)return check(false,"single-line table controls");
  selector->setCurrentIndex(1);QCoreApplication::processEvents();
  bool ok=true;
  auto checkBold=[&](QTextDocument* document){
    int cells=0;
    for(auto block=document->begin();block.isValid();block=block.next()){
      auto* nativeTable=QTextCursor(block).currentTable();if(!nativeTable)continue;
      const auto cell=nativeTable->cellAt(block.position());
      const bool expected=cell.row()==0||cell.column()==0||cell.column()==7;
      for(auto it=block.begin();!it.atEnd();++it){
        const auto fragment=it.fragment();if(!fragment.isValid()||fragment.text().isEmpty())continue;
        ok&=check((fragment.charFormat().fontWeight()>=QFont::Bold)==expected,"table headings, structure labels and selected columns preserve bold including subscripts and superscripts");
      }
      ++cells;
    }
    ok&=check(cells==16,"bold formatting checked in every table cell");
  };
  checkBold(browser->document());
  auto checkLines=[&]{
    browser->document()->documentLayout()->documentSize();
    int cells=0;
    for(auto block=browser->document()->begin();block.isValid();block=block.next()){
      if(!QTextCursor(block).currentTable())continue;
      ++cells;ok&=check(block.layout()->lineCount()==1,"each table cell stays on one line in a narrow preview");
    }
    ok&=check(cells==16,"all table header and data cells checked");
  };
  checkLines();
  clickClipboardButton(imageButton);
  const auto readImage=[]{return QImage::fromData(QApplication::clipboard()->mimeData()->data("image/png"),"PNG");};
  const auto baseline=readImage();
  model.tables[0].headers[0]="Structure with a very long header";
  model.tables[0].rows[0][0]="^{1}TS_{E} - long structure name\ncontinued on same line";
  model.tables[0].rows[0][1]="-12345678901234567890.12345678901234567890";
  preview.setDocumentModel(model);QCoreApplication::processEvents();checkLines();
  ok&=check(browser->horizontalScrollBar()->maximum()>0,"wide table scrolls horizontally instead of wrapping");
  clickClipboardButton(imageButton);const auto wide=readImage();
  // Different glyphs can change font descent and the tightly cropped height by
  // a few pixels; a wrapped data row would add an entire line instead.
  ok&=check(!wide.isNull()&&!baseline.isNull()&&wide.width()>baseline.width()&&std::abs(wide.height()-baseline.height())<=6,"long text and numbers expand image width without adding lines");
  if(qEnvironmentVariableIsSet("SI_NOWRAP_SAMPLE"))wide.save(qEnvironmentVariable("SI_NOWRAP_SAMPLE"));
  auto* tableButton=preview.findChild<QPushButton*>("copyPreviewTableButton");
  clickClipboardButton(tableButton);
  const auto html=QApplication::clipboard()->mimeData()->html();
  QTextDocument pasted;pasted.setHtml(html);checkBold(&pasted);
  ok&=check(html.contains("white-space:nowrap")&&!html.contains("<br>"),"HTML copy prevents wrapping and flattens line breaks within cells");
  return ok;
}
}

struct Phase1Smoke {
  static bool reactionWorkspace(const QString& fixture){
    MainWindow window;window.show();QTemporaryDir folder;bool ok=true;QStringList paths;for(int i=0;i<6;++i){const auto path=folder.filePath(QString("workspace-%1.log").arg(i));QFile::copy(fixture,path);paths.append(path);}window.parsePaths({paths[0],paths[1],paths[2],paths[3]},false,false);if(!waitForImport(window))return false;
    window.reactionStages_={QJsonObject{{"name","Reaction A"},{"files",QJsonArray{paths[2],paths[0]}},{"reference",paths[0]}},QJsonObject{{"name","Reaction B"},{"files",QJsonArray{paths[0],paths[1]}},{"reference",paths[1]}}};window.activeStage_=0;window.refreshDocumentModel();
    ok&=check(!window.files_->isVisible()&&window.stageGrid_->columnCount()==7&&window.stageNavigation_->count()==2&&window.stageGrid_->rowCount()==2&&window.stageGrid_->item(0,1)->text()=="workspace-2.log","persistent stage workspace filters structures and hides extraction columns");
    const auto master=window.sourcePaths();window.stageGrid_->setCurrentCell(0,0);window.moveStageStructure(1);auto first=window.reactionStages_[0].toObject();ok&=check(first["files"].toArray()==QJsonArray({paths[0],paths[2]})&&window.sourcePaths()==master&&window.reactionStages_[1].toObject()["files"].toArray()==QJsonArray({paths[0],paths[1]}),"structure order is independent for each reaction");
    window.refreshDocumentModel();window.chooseStageScope(1);auto* selector=window.preview_->findChild<QComboBox*>("previewTableSelector");bool onlyB=selector->count()>1;for(int i=1;i<selector->count();++i)onlyB&=selector->itemText(i).contains("Reaction B");ok&=check(onlyB,"current reaction preview includes only that reaction tables");window.previewScope_->setCurrentIndex(1);ok&=check(selector->count()==window.documentModel_.tables.size()+1,"whole SI preview retains all reaction tables");
    window.previewScope_->setCurrentIndex(0);window.stageGrid_->setCurrentCell(0,0);window.stageGrid_->item(0,0)->setText("5DRC-TS");window.refreshDocumentModel();ok&=check(window.structureLabels()[0]=="5DRC-TS"&&window.reactionStages_[0].toObject()["files"].toArray().contains(paths[0]),"inline name edits are shared without moving structures");
    window.removeStageStructure();ok&=check(window.sourcePaths()==master&&window.reactionStages_[1].toObject()["files"].toArray()==QJsonArray({paths[1]})&&window.reactionStages_[0].toObject()["files"].toArray().contains(paths[0]),"removing a member preserves shared source and other reaction membership");
    window.parsePaths({paths[3]},true,false);ok&=check(window.files_->rowCount()==4&&window.reactionStages_[1].toObject()["files"].toArray().contains(paths[3]),"existing file import attaches it to the selected reaction without duplication");
    window.parsePaths({paths[4]},true,false);window.parsePaths({paths[5]},true,false);window.chooseStageScope(0);if(!waitForImport(window))return false;ok&=check(window.reactionStages_[1].toObject()["files"].toArray().contains(paths[4])&&window.reactionStages_[1].toObject()["files"].toArray().contains(paths[5])&&!window.reactionStages_[0].toObject()["files"].toArray().contains(paths[5]),"queued imports retain their target reaction when navigation changes");
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ok&=check(dialog&&dialog->objectName()=="extractionDetailsDialog"&&dialog->findChild<QTableWidget*>()->columnCount()==7,"extraction metadata is available in its own optional dialog");if(dialog)dialog->reject();});window.showExtractionDetails();
    window.energyBasis_->setCurrentIndex(window.energyBasis_->findData(static_cast<int>(si::EnergyBasis::Electronic)));window.refreshDocumentModel();
    auto render=[&]{QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(auto* next=dialog?dialog->findChild<QPushButton*>("nextFigureGroup"):nullptr){for(int page=0;page<10&&dialog->isVisible();++page)next->click();}else {ok=false;if(dialog)dialog->reject();}});return window.prepareFigures();};
    ok&=check(render(),"current reaction Figure preparation succeeds");window.chooseStageScope(1);window.stageFigureCaptions_->setPlainText("Reaction B figure one\nReaction B figure two");ok&=check(render(),"second reaction Figure preparation succeeds independently");window.refreshDocumentModel();ok&=check(window.documentModel_.figures.size()==3&&window.documentModel_.figures[0].stageId!=window.documentModel_.figures.last().stageId,"rendered Figures keep reaction membership");
    QString error;const auto docx=folder.filePath("grouped.docx");ok&=check(DocxWriter::writeDocument(docx,window.documentModel_,&error),"reaction grouped SI generated");QFile output(docx);if(output.open(QIODevice::ReadOnly)){const auto bytes=output.readAll();ok&=check(bytes.indexOf("r:embed=\"rIdFigure1\"")<bytes.indexOf("Reaction A: Relative")&&bytes.indexOf("Reaction A: Relative")<bytes.indexOf("r:embed=\"rIdFigure2\"")&&bytes.indexOf("r:embed=\"rIdFigure2\"")<bytes.indexOf("Reaction B: Relative"),"DOCX places each reaction Figure before its own tables");}else ok=false;
    const auto project=folder.filePath("workspace.si_project.json");ok&=check(window.saveProjectToPath(project),"reaction workspace saves");ProjectState loaded;ok&=check(ProjectStore::load(project,&loaded,&error)&&loaded.reactionStages==window.reactionStages_,"reaction order and Figure settings roundtrip");
    // A mixed atom count must never silently produce a huge Hartree-based relative energy.
    window.chooseStageScope(0);
    const auto original=window.parsed_.value(paths[2]);
    if(original){
      auto changed=std::make_shared<si::GaussianResult>(*original);
      if(!changed->finalGeometry.empty())changed->finalGeometry.push_back(changed->finalGeometry.back());
      window.parsed_[paths[2]]=changed;
    }
    window.refreshDocumentModel();
    ok&=check(window.stageNeedsManualRelative(0)&&window.stageManualRelativeToggle_->isChecked()
      &&window.stageManualRelativeValues_->isVisible(),"atom-count mismatch automatically opens per-stage relative energy editor");
    ok&=check(!window.preflightErrors().isEmpty(),"mixed atom-count relative energy requires manual values");
    window.stageManualRelativeValues_->setPlainText("0.00\n12.50");
    window.refreshDocumentModel();
    bool hasManual=false;
    for(const auto& table:window.documentModel_.tables){
      if(table.section==si::DocumentSection::RelativeEnergy&&table.stageId==window.reactionStages_[0].toObject().value("id").toString(window.reactionStages_[0].toObject().value("name").toString())){
        hasManual=table.headers.size()==2&&table.rows.size()==2&&table.rows[0].value(1)=="0.00"&&table.rows[1].value(1)=="12.50";
      }
    }
    ok&=check(hasManual,"mixed atom-count reaction preview uses explicit kcal/mol entries only");
    const auto manualProject=folder.filePath("manual-relative.si_project.json");
    ok&=check(window.saveProjectToPath(manualProject),"manual relative values save with project");
    ProjectState reopened;ok&=check(ProjectStore::load(manualProject,&reopened,&error)
      &&reopened.reactionStages[0].toObject().value("manual_relative_values").toString()=="0.00\n12.50","stage manual values persist through project roundtrip");
    const auto existingSources=window.sourcePaths();
    QTimer::singleShot(0,&window,[]{
      if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        if(auto* yes=box->button(QMessageBox::Yes))yes->click();
    });
    window.clearStageStructures();
    ok&=check(window.reactionStages_[0].toObject().value("files").toArray().isEmpty()
      &&window.sourcePaths()==existingSources
      &&window.reactionStages_[1].toObject().value("files").toArray().size()>0,
      "clear stage only removes membership; all source files and other stages survive");
    if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR")){window.chooseStageScope(0);window.grab().save(qEnvironmentVariable("SI_NAV_SAMPLE_DIR")+"/reaction-workspace.png");}
    return ok;
  }

  static bool stageStructureThermodynamics(const QString& fixture){
    MainWindow window;window.show();QTemporaryDir folder;bool ok=true;QStringList paths;
    for(int i=0;i<3;++i){auto path=folder.filePath(QString("thermo-%1.log").arg(i));if(!QFile::copy(fixture,path))return false;paths.append(path);}
    window.parsePaths(paths,false,false);if(!waitForImport(window))return false;
    for(int i=0;i<3;++i){auto data=std::make_shared<si::GaussianResult>(*window.parsed_.value(paths[i]));
      data->scfEnergy=-100.0+i*0.01;data->zeroPointKcal=10.0+i;data->thermalTotalKcal=12.0+i;data->entropy=50.0+i;
      if(i==1)data->finalGeometry.push_back(data->finalGeometry.back());window.parsed_[paths[i]]=data;
    }
    window.reactionStages_={
      QJsonObject{{"id","mixed"},{"name","Mixed"},{"files",QJsonArray{paths[0],paths[1],paths[2]}},{"reference",paths[0]}},
      QJsonObject{{"id","shared"},{"name","Shared"},{"files",QJsonArray{paths[1]}},{"reference",paths[1]}},
      QJsonObject{{"id","normal"},{"name","Normal"},{"files",QJsonArray{paths[0],paths[2]}},{"reference",paths[0]}}
    };
    window.energyBasis_->setCurrentIndex(window.energyBasis_->findData(static_cast<int>(si::EnergyBasis::Electronic)));
    window.chooseStageScope(0);window.stageStructureRelativeToggle_->setChecked(true);window.refreshDocumentModel();
    auto relative=[&](const QString& id){for(const auto& table:window.documentModel_.tables)if(table.section==si::DocumentSection::RelativeEnergy&&table.stageId==id)return table;return si::DocumentTable{};};
    const auto normalBefore=relative("normal"),sharedBefore=relative("shared");const auto sources=window.parsed_;
    auto mixed=relative("mixed");ok&=check(mixed.headers.size()==8&&mixed.rows.size()==3&&mixed.rows[1].last()=="입력 필요"&&!window.preflightErrors().isEmpty(),"per-structure mode requires only incompatible structures to be corrected");
    const auto referenceBefore=mixed.rows[0],compatibleBefore=mixed.rows[2];
    auto isolatedStage=window.reactionStages_[0].toObject();isolatedStage["structure_relative_values"]=QJsonObject{{paths[2],QJsonObject{{"ΔE","42.00"}}}};
    auto expectedCompatible=compatibleBefore;expectedCompatible[1]="42.00";
    ok&=check(window.stageRelativeRow(isolatedStage,paths[2],mixed.headers,compatibleBefore)==expectedCompatible,"editing one electronic component preserves every other automatic value");

    window.stageGrid_->setCurrentCell(1,0);
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog){ok=false;return;}
      ok&=check(dialog->objectName()=="stageStructureRelativeDialog","selected structure energy editor opens");
      dialog->findChild<QLineEdit*>("stageRelativeValue_8")->setText("15.25");
      dialog->findChild<QLineEdit*>("stageRelativeValue_3")->setText("1.25");
      dialog->findChild<QLineEdit*>("stageRelativeValue_5")->setText("-2.50");
      dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });window.findChild<QPushButton*>("editStageStructureRelative")->click();window.refreshDocumentModel();
    mixed=relative("mixed");ok&=check(mixed.rows[1].last()=="15.25"&&mixed.rows[1][mixed.headers.indexOf("ΔZ_{0}")]=="1.25"&&mixed.rows[1][mixed.headers.indexOf("-TΔS^{b}")]=="-2.50"&&mixed.rows[1][1]=="N/A","selected row accepts Gibbs and individual thermodynamic components without using incompatible automatic energies");
    ok&=check(mixed.rows[0]==referenceBefore&&mixed.rows[2]==compatibleBefore&&relative("shared").rows==sharedBefore.rows&&relative("normal").rows==normalBefore.rows&&window.parsed_==sources,"other structures, stages and original Gaussian values remain unchanged");
    ok&=check(window.preflightErrors().isEmpty(),"a single corrected mismatched structure satisfies export checks");
    const auto savedStage=window.reactionStages_[0];
    ok&=check(relative("mixed").hasManualRelativeValues&&!relative("normal").hasManualRelativeValues&&si::relativeGibbsFootnote(relative("mixed")).contains("used as supplied"),"manual assignments are identified only in the corrected table and its footnote");
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog){ok=false;return;}
      auto* field=dialog->findChild<QLineEdit*>("stageRelativeValue_8");field->setText("nan");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
      ok&=check(dialog->isVisible()&&!dialog->findChild<QLabel*>("stageRelativeError")->text().isEmpty(),"nonfinite input is rejected without closing the editor");dialog->reject();
    });window.editStageStructureRelative();ok&=check(window.reactionStages_[0]==savedStage,"cancelling invalid edit preserves previous values");
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog){ok=false;return;}
      dialog->findChild<QLineEdit*>("stageRelativeValue_8")->clear();dialog->findChild<QLineEdit*>("stageRelativeValue_2")->setText("10.00");
      dialog->findChild<QLineEdit*>("stageRelativeValue_4")->setText("2.00");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });window.editStageStructureRelative();window.refreshDocumentModel();
    ok&=check(relative("mixed").rows[1].last()=="10.75"&&window.preflightErrors().isEmpty(),"Gibbs energy sums all supplied thermal components when explicit Gibbs is blank");
    window.includeDispersion_->setChecked(true);window.refreshDocumentModel();
    ok&=check(relative("mixed").rows[1].last()=="입력 필요"&&!window.preflightErrors().isEmpty(),"enabled dispersion must be supplied for an incompatible structure");
    window.includeDispersion_->setChecked(false);window.refreshDocumentModel();
    window.stageGrid_->setCurrentCell(1,0);window.moveStageStructure(1);window.stageGrid_->item(2,0)->setText("renamed-thermo");window.refreshDocumentModel();
    ok&=check(relative("mixed").rows[2][0]=="renamed-thermo"&&relative("mixed").rows[2].last()=="10.75","overrides follow the file when the structure is reordered and renamed");
    QString error;const auto project=folder.filePath("thermodynamics.json");ok&=check(window.saveProjectToPath(project),"per-structure energy project saves");
    MainWindow reopened;ok&=check(reopened.loadProjectPath(project)&&waitForImport(reopened),"per-structure energy project reopens");reopened.refreshDocumentModel();
    bool restored=false;for(const auto& table:reopened.documentModel_.tables)if(table.section==si::DocumentSection::RelativeEnergy&&table.stageId=="mixed")restored=table.rows[2].last()=="10.75";
    ok&=check(restored&&reopened.reactionStages_==window.reactionStages_,"per-stage path-linked overrides and preview survive save and reload");
    const auto document=folder.filePath("thermodynamics.docx");ok&=check(DocxWriter::writeDocument(document,window.documentModel_,&error),"per-structure thermal table exports to DOCX");
    QFile output(document);ok&=check(output.open(QIODevice::ReadOnly)&&output.readAll().contains(">10.75</w:t>"),"DOCX contains the corrected Gibbs value");
    window.chooseStageScope(1);ok&=check(!window.stageStructureRelativeToggle_->isChecked(),"structure input mode belongs only to its selected reaction");
    return ok;
  }

  static bool bulkTitlesAndStageContext(const QString& fixture){
    MainWindow window;QTemporaryDir directory;bool ok=true;
    QStringList paths;
    for(int i=0;i<3;++i){const auto path=directory.filePath(QString("structure-%1.out").arg(i));QFile::copy(fixture,path);paths.append(path);}
    window.parsePaths(paths,false,false);if(!waitForImport(window))return false;
    window.reactionStages_={
      QJsonObject{{"name","Initial"},{"files",QJsonArray{paths[0],paths[1],paths[2]}}},
      QJsonObject{{"name","Other stage"},{"files",QJsonArray{paths[0]}}}
    };
    window.refreshStageNavigation();window.chooseStageScope(0);
    ok&=check(window.stageNavigation_->contextMenuPolicy()==Qt::CustomContextMenu,"reaction navigation offers right-click context menu");
    auto* grid=window.stageGrid_;
    grid->setCurrentCell(1,0);
    QApplication::clipboard()->setText("^{3}TS_{1}\nIntermediate B");
    QTest::keyClick(grid,Qt::Key_V,Qt::ControlModifier);
    ok&=check(window.structureLabels()[1]=="^{3}TS_{1}"&&window.structureLabels()[2]=="Intermediate B"&&window.sourcePaths()==paths,"one-column Ctrl+V updates labels without changing output files");
    grid->setCurrentCell(0,0);QApplication::clipboard()->setText("Reactant with spaces");
    window.findChild<QPushButton*>("pasteStructureTitles")->click();
    ok&=check(window.structureLabels()[0]=="Reactant with spaces","explicit paste titles button accepts one title");
    const auto before=window.structureLabels();
    grid->setCurrentCell(2,0);QApplication::clipboard()->setText("Overflow one\nOverflow two");
    QTest::keyClick(grid,Qt::Key_V,Qt::ControlModifier);
    ok&=check(window.structureLabels()==before,"out-of-range one-column paste leaves all labels unchanged");
    window.stageNameEdit_->setFocus();window.stageNameEdit_->selectAll();
    QTest::keyClicks(window.stageNameEdit_,"First  stage (TS)  ");
    ok&=check(window.reactionStages_[0].toObject()["name"].toString()=="First  stage (TS)  ","reaction stage name preserves repeated and trailing spaces");
    QTimer::singleShot(0,&window,[&]{
      auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
      ok&=check(dialog!=nullptr,"right-click rename dialog appears");
      if(dialog){dialog->setTextValue("Renamed  step 1  ");dialog->accept();}
    });
    window.renameReactionStage(1);
    ok&=check(window.reactionStages_[1].toObject()["name"].toString()=="Renamed  step 1  ","rename preserves exact spacing and targets chosen stage");
    QTimer::singleShot(0,&window,[&]{
      auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      ok&=check(message!=nullptr,"delete confirmation appears");
      if(message&&message->button(QMessageBox::Yes))message->button(QMessageBox::Yes)->click();
    });
    window.removeReactionStage(1);
    ok&=check(window.reactionStages_.size()==1&&window.sourcePaths()==paths,"stage deletion retains Gaussian structures");
    const auto project=directory.filePath("bulk-titles.si_project.json");
    ok&=check(window.saveProjectToPath(project),"bulk titles and stage names saved");
    ProjectState loaded;QString error;
    ok&=check(ProjectStore::load(project,&loaded,&error)&&loaded.structureNames==window.structureLabels().join('\n')&&loaded.reactionStages.first().toObject()["name"].toString()=="First  stage (TS)  ","titles and stage name spacing survive project save");
    return ok;
  }
  static bool reactionStageNames(const QString& fixture){
    MainWindow window;QTemporaryDir folder;bool ok=true;const auto first=folder.filePath(QString::fromUtf8("원본 1.out")),second=folder.filePath("second.out");QFile::copy(fixture,first);QFile::copy(fixture,second);window.baseDirectory_->setText(folder.path());
    window.fileNames_->setPlainText(QString("1A\t%1\n3B\tsecond.out").arg(QFileInfo(first).fileName()));window.addNamedFiles();if(!waitForImport(window))return false;
    window.reactionStages_={QJsonObject{{"name","Initial"},{"files",QJsonArray{first,second}},{"reference",second}},QJsonObject{{"name","Next"},{"files",QJsonArray{first}},{"reference",first}}};
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog){ok=false;return;}auto* table=dialog->findChild<QTableWidget*>("reactionStageMembers");auto* stages=dialog->findChild<QListWidget*>("reactionStageList");auto* edit=dialog->findChild<QLineEdit*>("stageStructureNameEdit");
      ok&=check(table&&table->columnCount()==2&&!(table->item(0,1)->flags()&Qt::ItemIsEditable),"stage grid has editable structure names and linked output files");
      table->setCurrentCell(0,0);QApplication::clipboard()->setText(QString("^{7}E_{RC}-TS\tsecond\n5DCR-TS\t%1").arg(QFileInfo(first).fileName()));QTest::keyClick(table,Qt::Key_V,Qt::ControlModifier);
      ok&=check(table->item(0,0)->text()=="5DCR-TS"&&table->item(1,0)->text()=="^{7}E_{RC}-TS","two-column paste maps by output filename rather than clipboard row order");
      ok&=check(dialog->findChild<QLabel*>("stageStructureNamePreview")->text().contains("<sup>5</sup>")&&dialog->findChild<QLabel*>("stageStructureNamePreview")->text().contains("<sub>CR</sub>"),"automatic multiplicity and suffix markup preview");
      edit->setSelection(0,1);dialog->findChild<QPushButton*>("stageSuperscriptButton")->click();edit->setSelection(edit->text().indexOf("CR"),2);dialog->findChild<QPushButton*>("stageSubscriptButton")->click();
      ok&=check(table->item(0,0)->text()=="^{5}D_{CR}-TS","partial text selection applies explicit superscript and subscript");
      QApplication::clipboard()->setText("9X\tmissing.out");dialog->findChild<QPushButton*>("pasteStageStructures")->click();ok&=check(dialog->findChild<QLabel*>("stagePasteFeedback")->text().contains("missing.out")&&table->item(1,0)->text()=="^{7}E_{RC}-TS","unmatched output reports a problem without changing existing assignments");
      stages->setCurrentRow(1);ok&=check(table->item(0,0)->text()=="^{5}D_{CR}-TS"&&table->item(0,0)->checkState()==Qt::Checked&&table->item(1,0)->checkState()==Qt::Unchecked,"names shared across stages while membership remains independent");
      if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR"))dialog->grab().save(qEnvironmentVariable("SI_NAV_SAMPLE_DIR")+"/stage-name-grid.png");
      dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.editReactionStages();ok&=check(window.structureLabels()==QStringList({"^{5}D_{CR}-TS","^{7}E_{RC}-TS"})&&window.reactionStages_[0].toObject()["reference"]==second,"accepted names propagate globally without changing reference paths");
    const auto expected=window.structureLabels();window.applyNamedLabelsForExport();ok&=check(window.structureLabels()==expected,"export does not overwrite manual names with stale imported labels");window.refreshDocumentModel();ok&=check(window.documentModel_.coordinates[0].structureName==expected[0]&&window.documentModel_.tables[0].rows[0][0]==expected[0],"names reach coordinate and stage table models");
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());dialog->findChild<QTableWidget*>("reactionStageMembers")->item(0,0)->setText("Cancelled name");dialog->reject();});window.editReactionStages();ok&=check(window.structureLabels()==expected,"cancelling stage editor discards name changes");
    const auto project=folder.filePath("names.si_project.json");ok&=check(window.saveProjectToPath(project),"stage name project saved");ProjectState saved;QString error;ok&=check(ProjectStore::load(project,&saved,&error)&&saved.structureNames==expected.join('\n'),"superscript and subscript markup survives project roundtrip");
    window.fileNames_->setPlainText("3F\tsecond.out");window.applyNamedLabelsForExport();ok&=check(window.structureLabels()[1]=="3F","new pasted labels can still update names at export");
    return ok;
  }
  static bool projectManagerWorkflow(){
    QTemporaryDir root;bool ok=true;ProjectManagerDialog manager;
    ok&=check(!manager.isVisible()&&manager.findChild<QListWidget*>("recentProjectsList"),"project manager has a recent project list");
    manager.findChild<QLineEdit*>("newProjectName")->setText("Alpha SI");manager.findChild<QLineEdit*>("newProjectLocation")->setText(root.path());manager.findChild<QPushButton*>("createProjectButton")->click();const auto alpha=manager.selectedProject();
    ProjectState state;QString error;ok&=check(manager.result()==QDialog::Accepted&&ProjectStore::load(alpha,&state,&error)&&state.projectName=="Alpha SI"&&state.energyBasis=="5"&&!state.includeDispersion&&!state.includeComplexation,"new managed project persisted with expected defaults");
    MainWindow editor;ok&=check(editor.loadProjectPath(alpha)&&editor.windowTitle().contains("Alpha SI"),"selected project opens in named editor");editor.referencesText_->setPlainText("Alpha references");editor.authors_->setText("Alpha author");
    ProjectManagerDialog second;second.findChild<QLineEdit*>("newProjectName")->setText("Beta SI");second.findChild<QLineEdit*>("newProjectLocation")->setText(root.path());second.findChild<QPushButton*>("createProjectButton")->click();const auto beta=second.selectedProject();
    ok&=check(editor.loadProjectPath(beta)&&editor.referencesText_->toPlainText().isEmpty()&&editor.authors_->text().isEmpty(),"switch saves previous project and isolates document fields");
    ok&=check(ProjectStore::load(alpha,&state,&error)&&state.referencesText=="Alpha references"&&state.authors=="Alpha author","previous project saved on switch");
    editor.referencesText_->setPlainText("Beta references");editor.show();editor.close();ok&=check(ProjectStore::load(beta,&state,&error)&&state.referencesText=="Beta references","editor close saves current project");
    ProjectManagerDialog recent;auto* list=recent.findChild<QListWidget*>("recentProjectsList");ok&=check(list->count()>=2&&list->item(0)->text().contains("Beta SI"),"recent list updates after creation and save");
    if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR")){recent.show();QCoreApplication::processEvents();recent.grab().save(qEnvironmentVariable("SI_NAV_SAMPLE_DIR")+"/project-manager.png");}
    recent.findChild<QPushButton*>("openSelectedProject")->click();ok&=check(recent.selectedProject()==beta&&recent.result()==QDialog::Accepted,"recent project selection opens selected project");
    ok&=check(editor.loadProjectPath(alpha)&&editor.referencesText_->toPlainText()=="Alpha references","project can be reopened without cross-project drafts");
    QSettings prefs("SI Generator","SIGeneratorTests");const auto originalRecent=prefs.value("recentProjects");
    for(int i=0;i<34;++i){state.projectName=QString("Library %1").arg(i);const auto path=root.filePath(QString("library-%1.si_project.json").arg(i));ok&=check(ProjectStore::save(path,state,&error),"pagination fixture saved");ProjectManagerDialog::rememberProject(path);}
    const int librarySize=originalRecent.toStringList().size()+34;
    ok&=check(prefs.value("recentProjects").toStringList().size()==librarySize,"project history retains more than thirty projects");
    ProjectManagerDialog library;library.show();QCoreApplication::processEvents();auto* cards=library.findChild<QListWidget*>("recentProjectsList");auto* next=library.findChild<QPushButton*>("nextProjectPage");auto* previous=library.findChild<QPushButton*>("previousProjectPage");
    const auto firstPath=cards->item(0)->data(Qt::UserRole).toString();
    ok&=check(library.screen()->availableGeometry().contains(library.frameGeometry()),"project manager opens entirely inside the available desktop");
    const auto normalSize=library.size();library.resize(640,420);QCoreApplication::processEvents();
    auto* managerScroll=library.findChild<QScrollArea*>("projectManagerScroll");auto* create=library.findChild<QPushButton*>("createProjectButton");
    ok&=check(library.width()==640&&library.height()==420&&managerScroll&&managerScroll->verticalScrollBar()->maximum()>0,"project manager can shrink and scroll on a small desktop");
    managerScroll->ensureWidgetVisible(create);QCoreApplication::processEvents();
    ok&=check(managerScroll->viewport()->rect().contains(create->mapTo(managerScroll->viewport(),create->rect().center())),"create project button remains reachable after shrinking");
    library.resize(normalSize);managerScroll->verticalScrollBar()->setValue(0);QCoreApplication::processEvents();
    ok&=check(cards->count()==3&&next->isEnabled()&&!previous->isEnabled()&&cards->verticalScrollBar()->maximum()==0,"project list shows a fixed page without vertical scrolling");
    QTest::mouseClick(cards->viewport(),Qt::LeftButton,Qt::NoModifier,cards->visualItemRect(cards->item(1)).center());ok&=check(cards->currentRow()==1,"project card text area selects its project");
    next->click();const auto pinnedPath=cards->item(0)->data(Qt::UserRole).toString();ok&=check(pinnedPath!=firstPath&&previous->isEnabled(),"next page changes projects");
    auto* pin=cards->itemWidget(cards->item(0))->findChild<QToolButton*>("pinProject");pin->click();
    ok&=check(cards->item(0)->data(Qt::UserRole).toString()==pinnedPath&&!previous->isEnabled(),"pinning moves project to top of first page");
    cards->itemWidget(cards->item(0))->findChild<QToolButton*>("favoriteProject")->click();library.findChild<QToolButton*>("projectTheme2")->click();
    ProjectManagerDialog restored;auto* restoredCards=restored.findChild<QListWidget*>("recentProjectsList");
    ok&=check(restoredCards->item(0)->data(Qt::UserRole).toString()==pinnedPath&&restoredCards->itemWidget(restoredCards->item(0))->findChild<QToolButton*>("favoriteProject")->isChecked()&&restored.findChild<QToolButton*>("projectTheme2")->isChecked(),"pin heart and theme persist across project managers");
    if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR")){restored.show();QCoreApplication::processEvents();restored.grab().save(qEnvironmentVariable("SI_NAV_SAMPLE_DIR")+"/project-manager-lavender.png");}
    restoredCards->itemWidget(restoredCards->item(0))->findChild<QToolButton*>("pinProject")->click();ok&=check(restoredCards->item(0)->data(Qt::UserRole).toString()==firstPath,"unpin restores recent project order");
    for(int i=0;i<(librarySize+2)/3;++i)next->click();ok&=check(!next->isEnabled()&&cards->count()==(librarySize-1)%3+1,"last project page disables next button");
    previous->click();ok&=check(next->isEnabled(),"previous page restores next navigation");
    prefs.setValue("recentProjects",originalRecent);prefs.remove("pinnedProjects");prefs.remove("favoriteProjects");prefs.remove("projectManagerTheme");
    return ok;
  }
  static bool portableProject(const QString& fixture){
    MainWindow source;QTemporaryDir temp;bool ok=true;
    const auto primary=temp.filePath("1-H2O-freq.log");
    const auto singlePoint=temp.filePath("1-H2O-freq-bb.log");
    ok&=check(QFile::copy(fixture,primary),"portable project input fixture copied");
    QFile spe(singlePoint);
    if(!spe.open(QIODevice::WriteOnly))return false;
    spe.write("#p b3lyp/def2tzvpp sp\n Charge = 0 Multiplicity = 1\n"
              " SCF Done: E(RB3LYP) = -76.500000 A.U.\n Normal termination of Gaussian 16\n");
    spe.close();
    source.parsePaths({primary},false,false);
    if(!waitForImport(source))return false;
    source.files_->item(0,0)->setText("H2O reference");
    source.includeAbsolute_->setChecked(true);
    source.siOutputPath_->setText(temp.filePath("removed-folder/water.docx"));
    source.xyzOutputPath_->setText(temp.filePath("removed-folder/water.xyz"));
    source.reactionStages_={QJsonObject{{"name","Water"},{"files",QJsonArray{primary}},
                                         {"reference",primary}}};
    const auto project=temp.filePath("portable.si_project.json");
    ok&=check(source.saveProjectToPath(project),"portable project saved");
    ProjectState stored;QString error;
    const bool saved=ProjectStore::load(project,&stored,&error);
    const auto linkedSpe=stored.pairedSnapshotPaths.value(primary).toString();
    ok&=check(saved&&stored.gaussianSnapshots.contains(primary)&&
      QFileInfo(linkedSpe).absoluteFilePath()==QFileInfo(singlePoint).absoluteFilePath()&&
      stored.gaussianSnapshots.contains(linkedSpe),
      "both frequency and SPE are embedded");
    // An old project snapshot must not hide corrected source data when the
    // Gaussian output is available locally.
    QFile updated(primary);if(!updated.open(QIODevice::ReadOnly))return false;
    auto newOutput=updated.readAll();updated.close();
    ok&=check(newOutput.contains("0.100000"),"live-source spin fixture located");
    newOutput.replace("0.100000","0.888888");
    if(!updated.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
    updated.write(newOutput);updated.close();
    {
      MainWindow refreshed;
      ok&=check(refreshed.loadProjectPath(project)&&waitForImport(refreshed),
        "saved project reloads with available Gaussian source");
      const auto live=refreshed.energyRecords();
      ok&=check(live.size()==1&&!live[0].frequency.spinDensities.empty()&&
        std::abs(live[0].frequency.spinDensities[0].spin-0.888888)<1e-9,
        "fresh Gaussian spin values supersede stale embedded snapshots");
    }
    ok&=check(QFile::remove(primary)&&QFile::remove(singlePoint),"original Gaussian files removed");
    MainWindow reopened;
    ok&=check(reopened.loadProjectPath(project)&&waitForImport(reopened),"offline project reopens");
    ok&=check(reopened.siOutputPath_->text()==temp.filePath("water.docx")&&
      reopened.xyzOutputPath_->text()==temp.filePath("water.xyz"),
      "moved project redirects missing output directories into project folder");
    const auto records=reopened.energyRecords();
    ok&=check(records.size()==1&&records[0].frequency.finalGeometry.size()==3&&
      records[0].frequency.scfEnergy==std::optional<double>(-76.4)&&
      records[0].singlePoint&&records[0].singlePoint->scfEnergy==std::optional<double>(-76.5),
      "offline structure, energy, and paired SPE restored");
    ok&=check(reopened.structureLabels()==QStringList{"H2O reference"}&&
      reopened.reactionStages_.size()==1&&
      reopened.reactionStages_.first().toObject()["reference"].toString()==primary,
      "offline labels and reaction assignments restored");
    const auto model=reopened.buildDocumentModel();
    ok&=check(model.coordinates.size()==1&&model.coordinates[0].atoms.size()==3&&
      !model.tables.isEmpty(),"offline SI document rebuilt without output files");
    const auto docx=temp.filePath("offline.docx");
    ok&=check(DocxWriter::writeDocument(docx,model,&error)&&QFileInfo::exists(docx),
      "offline project exports DOCX without output files");
    return ok;
  }
  static bool reactionStageWorkflow(const QString& fixture){
    MainWindow window;QTemporaryDir temp;bool ok=true;
    ok&=check(!window.includeDispersion_->isChecked()&&!window.includeComplexation_->isChecked(),"new SI starts with optional corrections off");
    QStringList paths;for(int i=0;i<4;++i){const auto path=temp.filePath(QString("stage-%1.log").arg(i));QFile::copy(fixture,path);paths.append(path);}window.parsePaths(paths,false,false);if(!waitForImport(window))return false;
    for(int i=0;i<paths.size();++i){auto parsed=std::make_shared<si::GaussianResult>(*window.parsed_.value(paths[i]));parsed->scfEnergy=-100.0+0.01*i;parsed->zeroPointCorrection=0.01;parsed->thermalCorrection=0.02;parsed->entropy=50.0;window.parsed_[paths[i]]=parsed;}
    auto stage=[&](QString name,QJsonArray files,QString ref){return QJsonObject{{"name",name},{"files",files},{"reference",ref}};};
    window.includeAbsolute_->setChecked(true);
    window.reactionStages_={stage("First stage",{paths[0],paths[1]},paths[1]),stage("Second stage",{paths[1],paths[2]},paths[2])};
    auto model=window.buildDocumentModel();int relative=0;for(const auto& table:model.tables)if(table.section==si::DocumentSection::RelativeEnergy){++relative;ok&=check(table.rows.size()==(relative==3?1:2),"stage membership and unassigned structure retained");if(relative<3)ok&=check(table.rows.last().last()=="0.00","each stage uses its own reference");}
    ok&=check(relative==3&&model.tables[0].caption.startsWith("First stage")&&std::any_of(model.tables.begin(),model.tables.end(),[](const auto& t){return t.caption.startsWith("Second stage");}),"tables grouped in stage order");
    QTimer::singleShot(0,&window,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog){ok=false;return;}auto* list=dialog->findChild<QListWidget*>("reactionStageList");list->setCurrentRow(1);dialog->findChild<QPushButton*>("stageMoveUp")->click();if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR"))dialog->grab().save(qEnvironmentVariable("SI_NAV_SAMPLE_DIR")+"/stage-designer.png");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    window.editReactionStages();model=window.buildDocumentModel();ok&=check(window.reactionStages_.first().toObject()["name"]=="Second stage"&&model.tables.first().caption.startsWith("Second stage"),"moving a stage reorders its complete table group");
    ProjectState state;state.reactionStages=window.reactionStages_;QString error;ProjectState loaded;const auto project=temp.filePath("stages.json");ok&=check(ProjectStore::save(project,state,&error)&&ProjectStore::load(project,&loaded,&error)&&loaded.reactionStages==state.reactionStages,"stage order and reference roundtrip");
    const auto output=temp.filePath("stages.docx");ok&=check(DocxWriter::writeDocument(output,model,&error),"stage DOCX generated");QFile file(output);ok&=check(file.open(QIODevice::ReadOnly),"stage DOCX readable");const auto bytes=file.readAll();ok&=check(bytes.indexOf("Second stage: Absolute")<bytes.indexOf("Second stage: Relative")&&bytes.indexOf("Second stage: Relative")<bytes.indexOf("First stage: Absolute"),"DOCX preserves complete stage order");
    window.energyInputMode_->setCurrentIndex(window.energyInputMode_->findData("manual"));window.includeAbsolute_->setChecked(false);
    window.manualRelativeNames_->setPlainText("stage-2\nstage-1\nstage-0\nstage-3");
    window.manualRelativeValues_->setPlainText("2 0 2 0 0 0 2\n1 0 1 0 0 0 1\n0 0 0 0 0 0 0\n3 0 3 0 0 0 3");model=window.buildDocumentModel();
    ok&=check(model.tables.first().rows.first().first()=="stage-1"&&model.tables.first().rows.last().last()=="2.00","manual table assignment follows names rather than input row position");
    return ok;
  }
  static bool iconNavigation(MainWindow& window){
    bool ok=true;
    auto* bar=window.findChild<QToolBar*>("quickToolbar");auto* split=window.findChild<QSplitter*>("mainSplitter");
    ok&=check(bar&&window.toolBarArea(bar)==Qt::TopToolBarArea&&split&&split->count()==3,"top toolbar releases the right workspace column");
    auto* tabs=window.findChild<QTabWidget*>("workspaceTabs");
    auto* focus=window.findChild<QToolButton*>("focusWorkspace");
    ok&=check(tabs&&tabs->count()==2&&focus,"tabbed viewer and focus action are available");
    if(tabs){
      tabs->setCurrentIndex(1);QCoreApplication::processEvents();
      ok&=check(window.preview_->isVisible(),"SI preview tab displays the document");
      tabs->setCurrentIndex(0);QCoreApplication::processEvents();
      ok&=check(window.moleculeView_->isVisible(),"molecule tab displays the viewer");
    }
    if(focus){
      focus->click();QCoreApplication::processEvents();
      ok&=check(focus->isChecked()&&split->sizes()[2]>split->sizes()[1],"focus mode expands the workspace");
      focus->click();QCoreApplication::processEvents();
      ok&=check(!focus->isChecked()&&split->widget(0)->isVisible(),"focus mode restores navigation");
    }
    for(const auto& name:{"addFilesAction","saveProjectAction","generateSiButton"}){
      auto* action=window.findChild<QToolButton*>(name);
      ok&=check(action&&action->toolButtonStyle()==Qt::ToolButtonTextBesideIcon,"important actions include text labels");
    }
    const QStringList names={"structures","energy","spin","geometry","document"};
    for(int i=0;i<names.size();++i){
      auto* button=window.findChild<QToolButton*>("navigate_"+names[i]);
      if(!button){ok&=check(false,"navigation button exists");continue;}
      button->click();QCoreApplication::processEvents();
      ok&=check(button->isChecked()&&!button->icon().isNull()&&button->toolButtonStyle()==Qt::ToolButtonIconOnly&&!button->accessibleName().isEmpty(),"icon navigation has active state and accessible name");
      ok&=check(window.inputPages_->currentIndex()==i&&window.inputScroll_->isVisible(),"navigation displays the requested settings page");
      ok&=check(!window.files_->isVisible()&&window.stageGrid_->isVisible()==(i==0),"unselected settings do not fill the input panel");
      if(i==2)ok&=check(window.moleculeView_->interactionMode()==MoleculeViewWidget::InteractionMode::SpinDensity,"spin page activates spin picking");
      if(i==3)ok&=check(window.moleculeView_->interactionMode()==MoleculeViewWidget::InteractionMode::Geometry,"geometry page activates measurement picking");
    }
    window.referencesText_->setPlainText("Navigation draft persistence");window.scrollTo(window.filesSection_);window.scrollTo(window.documentSection_);
    ok&=check(window.referencesText_->toPlainText()=="Navigation draft persistence","navigation preserves edits");window.referencesText_->clear();window.scrollTo(window.filesSection_);
    return ok;
  }
  static bool waitForImport(MainWindow& window){return waitUntil([&]{return !window.parsing_;});}
  static bool responsiveImport(const QString& fixture){
    QTemporaryDir folder;bool ok=true;MainWindow window;
    const auto large=folder.filePath(QString::fromUtf8("큰 파일.log"));
    QFile file(large);if(!file.open(QIODevice::WriteOnly))return false;
    // Many ordinary lines model a large Gaussian output without a giant regex input.
    QByteArray block;for(int i=0;i<16000;++i)block+=" Population analysis placeholder 1234567890 ABCDEFGHIJKLMNOPQRSTUVWXYZ\n";
    file.write("#p b3lyp/def2-svp opt freq\n\n");
    for(int i=0;i<20;++i)file.write(block);
    file.write(" SCF Done: E = -100.0\n Normal termination of Gaussian 16\n");file.close();
    const auto small=folder.filePath("small.log");if(!QFile::copy(fixture,small))return false;
    window.parsePaths({large,small},true);
    ok&=check(window.parsing_&&window.sourcePaths()==QStringList{large,small}&&window.files_->item(0,6)->text().contains(QString::fromUtf8("대기")),"selected files appear immediately before background parsing completes");
    int ticks=0;QTimer heartbeat;heartbeat.setInterval(5);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;});heartbeat.start();
    ok&=check(waitUntil([&]{return window.parsed_.contains(large);},15000),"large Unicode-path import completes");
    ok&=check(ticks>0,"UI event loop responds during large-file import");
    ok&=check(waitUntil([&]{return !window.parsing_;}),"streaming import completes");
    window.parsePaths({large});
    window.parsePaths({large},true); // Superseded queued addition must be discarded.
    window.pendingLabelsByPath_.insert(small,"Replacement label");
    window.parsePaths({small});
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==QStringList{small}&&window.files_->item(0,0)->text()=="Replacement label","replacement during parsing wins and retains its labels");
    QTest::qWait(100);
    ok&=check(window.sourcePaths()==QStringList{small},"stale results cannot restore replaced files");
    window.parsePaths({large});window.parsePaths({});
    ok&=check(!window.parsing_&&window.sourcePaths().isEmpty(),"empty replacement cancels an active import");
    window.acceptPaths({folder.path()});window.parsePaths({small});
    ok&=check(waitUntil([&]{return !window.parsing_;}),"replacement after setting base folder completes");
    QTest::qWait(150);
    ok&=check(window.sourcePaths()==QStringList{small},"setting base folder cannot append into a replacement project");
    window.parsePaths({});
    QTimer::singleShot(0,&window,[&window,small]{
      if(auto* dialog=window.findChild<QFileDialog*>()){
        dialog->selectFile(small);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
      }
    });
    window.chooseFiles();
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==QStringList{small},"file dialog selection reaches the structure list");
    auto* closing=new MainWindow;closing->parsePaths({large});delete closing;
    QTest::qWait(100);
    return ok;
  }
  static bool automaticGeometry(){
    MainWindow window;QTemporaryDir folder;bool ok=true;
    QStringList paths;
    for(int index=0;index<2;++index){
      const auto path=folder.filePath(QString("reaction%1-freq.out").arg(index));QFile file(path);
      if(!file.open(QIODevice::WriteOnly))return false;
      file.write(QString(" # opt freq B3LYP/Def2-SVP\n\n Charge = 0 Multiplicity = 1\n SCF Done: E(RB3LYP) = -100.0 A.U.\n"
        " Standard orientation:\n -----\n Center Atomic Atomic Coordinates\n Number Number Type X Y Z\n -----\n"
        " 1 6 0 0 0 0\n 2 6 0 1.4 0 0\n 3 8 0 0 1.3 0\n 4 1 0 0 0 %1\n -----\n Mulliken charges and spin densities:\n 1 C 0.000 0.1234\n 2 C 0.000 0.7654\n 3 O 0.000 0.2345\n 4 H 0.000 0.0400\n Sum of Mulliken charges = 0\n Normal termination of Gaussian 16\n").arg(index?1.7:1.0).toUtf8());
      file.close();paths.append(path);
    }
    window.parsePaths(paths);if(!waitUntil([&]{return !window.parsing_;}))return false;
    window.decimalPlaces_->setValue(1); // A stale legacy setting must not round SI values to one decimal.
    window.includeAbsolute_->setChecked(true);
    window.refreshDocumentModel();
    const auto* absolute=window.documentModel_.table(si::DocumentSection::AbsoluteEnergy);
    const auto* relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    ok&=check(absolute&&absolute->rows[0].value(1)=="-100.00"&&
      relative&&relative->rows[0].value(1)=="0.00",
      "SI absolute and relative energies retain exactly two decimal places");
    auto* table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    const auto* originalSpin=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(window.documentModel_.precisionOverride==0&&originalSpin&&
      originalSpin->rows[0].value(1)=="0.765"&&table&&table->rows[0].value(1)=="1.000",
      "energy, spin and geometry use per-quantity precision despite a legacy one-digit setting");
    ok&=check(table&&table->rows.size()==2&&table->headers.contains("C(1)-H(4) (Å)")&&!table->caption.contains("RMSD")&&!table->caption.contains("Automatically suggested"),"empty geometry input produces automatic reaction distance table");
    ok&=check(window.geometryIndices_->toPlainText().isEmpty(),"automatic suggestions preserve empty input for recalculation");
    const auto spinWithNumbers=*originalSpin;
    const auto geometryWithNumbers=*table;
    window.spinAtomNumbers_->setChecked(false);window.refreshDocumentModel();
    auto* spinWithoutNumbers=window.documentModel_.table(si::DocumentSection::SpinDensity);
    table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    ok&=check(spinWithoutNumbers&&spinWithoutNumbers->headers[1]=="C"&&spinWithoutNumbers->rows==spinWithNumbers.rows&&table&&table->headers==geometryWithNumbers.headers,"spin numbers toggle independently without changing values");
    window.geometryAtomNumbers_->setChecked(false);window.refreshDocumentModel();
    table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    ok&=check(table&&table->headers.contains("C-H (Å)")&&table->rows==geometryWithNumbers.rows,"automatic geometry can hide numbers without changing values");
    const auto settingsPath=folder.filePath("atom-numbers.json");QString settingsError;ProjectState settings;
    ok&=check(window.saveProjectToPath(settingsPath)&&ProjectStore::load(settingsPath,&settings,&settingsError)&&!settings.spinAtomNumbers&&!settings.geometryAtomNumbers,"hidden atom numbers survive project save");
    {MainWindow reopened;ok&=check(reopened.loadProjectPath(settingsPath)&&!reopened.spinAtomNumbers_->isChecked()&&!reopened.geometryAtomNumbers_->isChecked(),"atom number options restored in UI");waitForImport(reopened);}
    QFile legacy(settingsPath);
    if(legacy.open(QIODevice::ReadOnly)){
      auto json=QJsonDocument::fromJson(legacy.readAll()).object();legacy.close();
      for(const auto& key:QStringList{"spin_settings","geometry_settings"}){auto section=json[key].toObject();section.remove("show_atom_numbers");json[key]=section;}
      if(legacy.open(QIODevice::WriteOnly|QIODevice::Truncate)){legacy.write(QJsonDocument(json).toJson());legacy.close();}
      ok&=check(ProjectStore::load(settingsPath,&settings,&settingsError)&&settings.spinAtomNumbers&&settings.geometryAtomNumbers,"legacy projects retain visible numbers by default");
    }else ok=false;
    window.geometryIndices_->setPlainText("1 2 3");window.refreshDocumentModel();
    table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    ok&=check(table&&table->headers.size()==2&&table->headers[1].contains("°")&&!table->caption.contains("RMSD"),"manual geometry overrides automatic suggestions");
    ok&=check(table&&table->headers[1]=="C-C-O (°)","manual geometry also hides atom numbers");
    window.spinAtomNumbers_->setChecked(true);window.geometryAtomNumbers_->setChecked(true);
    window.geometryIndices_->clear();window.parsePaths({paths.front()});if(!waitUntil([&]{return !window.parsing_;}))return false;
    window.refreshDocumentModel();
    ok&=check(!window.documentModel_.table(si::DocumentSection::SelectedGeometry)&&!window.geometryAutoInfo_->text().isEmpty(),"single structure shows explanation instead of automatic table");

    QStringList secondReaction;
    for(int index=0;index<2;++index){
      const auto path=folder.filePath(QString("other%1-freq.out").arg(index));QFile file(path);
      if(!file.open(QIODevice::WriteOnly))return false;
      file.write(QString(" # opt freq UB3LYP/Def2-SVP\n\n Charge = 0 Multiplicity = 3\n SCF Done: E(UB3LYP) = -101.0 A.U.\n"
        " Standard orientation:\n -----\n Center Atomic Atomic Coordinates\n Number Number Type X Y Z\n -----\n"
        " 1 8 0 0 0 0\n 2 6 0 1.4 0 0\n 3 1 0 0 0 %1\n -----\n"
        " Mulliken charges and spin densities:\n 1 O 0.000 0.6543\n 2 C 0.000 0.2345\n 3 H 0.000 0.1111\n"
        " Sum of Mulliken charges = 0\n Normal termination of Gaussian 16\n").arg(index?1.7:1.0).toUtf8());
      file.close();secondReaction.append(path);
    }
    auto allPaths=paths;allPaths.append(secondReaction);
    window.parsePaths(allPaths);if(!waitUntil([&]{return !window.parsing_;}))return false;
    window.reactionStages_={
      QJsonObject{{"name","First reaction"},{"files",QJsonArray{paths[0],paths[1]}},{"reference",paths[0]}},
      QJsonObject{{"name","Second reaction"},{"files",QJsonArray{secondReaction[0],secondReaction[1]}},{"reference",secondReaction[0]}}
    };
    window.refreshDocumentModel();
    int geometryTables=0,spinTables=0;
    for(const auto& scoped:window.documentModel_.tables){
      if(scoped.section!=si::DocumentSection::SelectedGeometry&&scoped.section!=si::DocumentSection::SpinDensity)continue;
      ok&=check(scoped.rows.size()==2,"each reaction table has two structures");
      for(const auto& row:scoped.rows)for(int col=1;col<row.size();++col)
        ok&=check(row[col]!="N/A","valid stage-specific XYZ/spin does not produce N/A");
      if(scoped.section==si::DocumentSection::SelectedGeometry)++geometryTables;
      else ++spinTables;
    }
    ok&=check(geometryTables==2&&spinTables==2,"automatic spin and geometry tables are generated separately for reactions with different atom counts");
    window.spinAtomNumbers_->setChecked(false);window.geometryAtomNumbers_->setChecked(false);window.refreshDocumentModel();
    for(const auto& scoped:window.documentModel_.tables){
      if(scoped.section!=si::DocumentSection::SelectedGeometry&&scoped.section!=si::DocumentSection::SpinDensity)continue;
      for(const auto& header:scoped.headers)ok&=check(!header.contains(QRegularExpression("\\(\\d+\\)")),"stage-specific automatic tables hide atom numbers");
    }

    return ok;
  }
  static bool labelledFileImports(const QString& fixture){
    MainWindow window;QTemporaryDir folder;bool ok=true;
    const QStringList names={QString::fromUtf8("원본 1 (freq).out"),"second.out","third.out"};
    QStringList paths;for(const auto& name:names){const auto path=folder.filePath(name);if(!QFile::copy(fixture,path))return false;paths.append(path);}
    window.baseDirectory_->setText(folder.path());
    const auto input=QString("(5DCR-TS \"%1\")\t(3DRC second)\r\n(1C third.out)\r\nmissing.out").arg(names[0]);
    window.fileNames_->setPlainText(input);window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==QStringList({"5DCR-TS","3DRC","1C"}),"label/file pairs support quoted Unicode paths, spaces, parentheses, tabs and optional extensions");
    ok&=check(window.fileNamesInfo_->text().contains("missing.out"),"missing labelled input retains diagnostics");
    window.refreshDocumentModel();
    ok&=check(window.documentModel_.coordinates.size()==3&&window.documentModel_.coordinates[0].structureName=="5DCR-TS"&&window.documentModel_.coordinates[1].structureName=="3DRC","imported labels propagate to Cartesian coordinate blocks");
    auto* table=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    ok&=check(table&&table->rows[0][0]=="5DCR-TS"&&table->rows[1][0]=="3DRC","imported labels propagate to energy tables");
    QString error;const auto xyzPath=folder.filePath("labels.xyz");
    ok&=check(si::XyzWriter::write(xyzPath,window.documentModel_,&error),"labelled XYZ write");QFile xyz(xyzPath);
    if(xyz.open(QIODevice::ReadOnly)){const auto data=xyz.readAll();ok&=check(data.contains("^{5}D_{CR}-TS |")&&data.contains("^{3}D_{RC} |")&&data.contains("^{1}C |"),"XYZ comment names preserve multiplicity and CR/RC markup");}else ok=false;
    ProjectState state;state.files=paths;state.structureNames=window.structureLabels().join('\n');
    ok&=check(ProjectStore::save(folder.filePath("labels.json"),state,&error),"labelled project save");ProjectState restored;
    ok&=check(ProjectStore::load(folder.filePath("labels.json"),&restored,&error)&&restored.structureNames==state.structureNames,"labelled project names roundtrip");
    window.fileNames_->setPlainText(QString("(7ERC %1)").arg(names[0]));window.addNamedFiles();
    ok&=check(window.files_->rowCount()==3&&window.files_->item(0,0)->text()=="7ERC","explicit label updates existing structure without duplicate import");
    window.parsePaths({paths[0]});window.fileNames_->setPlainText(input);window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==QStringList({"5DCR-TS","3DRC","1C"}),"labels survive append requests queued during background parsing");
    window.parsePaths({paths[0]});window.fileNames_->setPlainText("(3DRC second.out)");window.addNamedFiles();
    window.pendingLabelsByPath_.insert(paths[2],"9XRC-TS");window.parsePaths({paths[2]});
    ok&=check(waitForImport(window)&&window.structureLabels()==QStringList{"9XRC-TS"},"project replacement wins over queued labelled imports");
    window.parsePaths({paths[1]},true);
    ok&=check(waitForImport(window)&&window.structureLabels()==QStringList({"9XRC-TS","second"}),"cancelled queued labels do not leak into later imports");
    window.parsePaths({});window.fileNames_->clear();
    QApplication::clipboard()->setText(QString("5B-TS\t%1\r\n5C\tsecond.out\r\n3DRC\tthird.out").arg(names[0]));
    window.findChild<QGroupBox*>("importControls")->setChecked(true);window.fileNames_->paste();window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==QStringList({"5B-TS","5C","3DRC"})&&!window.fileNamesInfo_->text().contains("파일 없음"),"actual Excel label/file clipboard rows import without treating labels as missing filenames");
    window.fileNames_->setPlainText(QString("5DCR-TS    %1").arg(names[0]));window.addNamedFiles();
    ok&=check(window.files_->rowCount()==3&&window.structureLabels()[0]=="5DCR-TS","unparenthesized whitespace label/file pair relabels existing file");
    const auto literal=folder.filePath("1A.out");if(!QFile::copy(fixture,literal))return false;
    window.parsePaths({});window.fileNames_->setPlainText("1A\tsecond.out");window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({literal,paths[1]})&&window.structureLabels()==QStringList({"1A","second"}),"two-column actual filenames are not mistaken for a label/file row");
    const QString tsName="5-CoIII-Me3-TPADP-AfterCC-forOO_Cleavage-scan-freq-ts-freq.out";
    const QString ircName=tsName.chopped(4)+"-ircfwd-opt-freq.out";
    if(!QFile::copy(fixture,folder.filePath(tsName))||!QFile::copy(fixture,folder.filePath(ircName)))return false;
    window.parsePaths({});window.fileNames_->clear();
    QApplication::clipboard()->setText(QString("5B-TS\t%1\r\n5C\t%2").arg(tsName,ircName));window.findChild<QGroupBox*>("importControls")->setChecked(true);window.fileNames_->paste();window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({folder.filePath(tsName),folder.filePath(ircName)})&&window.structureLabels()==QStringList({"5B-TS","5C"})&&!window.fileNamesInfo_->text().contains("파일 없음"),"screenshot-shaped Excel paste preserves TS/IRC labels across automatic endpoint discovery");
    // Real Excel sends text/html as well as text/plain. QTextEdit must choose
    // the tab-delimited plain text instead of flattening the HTML table.
    window.parsePaths({});window.fileNames_->clear();
    auto* excelMime=new QMimeData;
    excelMime->setHtml(QString("<table><tr><td>5B-TS</td><td>%1</td></tr><tr><td>5C</td><td>%2</td></tr></table>").arg(tsName,ircName));
    excelMime->setText(QString("5B-TS\t%1\r\n5C\t%2").arg(tsName,ircName));
    QApplication::clipboard()->setMimeData(excelMime);
    window.fileNames_->paste();
    ok&=check(window.fileNames_->toPlainText().contains('\t'),"rich Excel clipboard preserves tab-delimited columns");
    window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({folder.filePath(tsName),folder.filePath(ircName)})
      &&window.structureLabels()==QStringList({"5B-TS","5C"})&&!window.fileNamesInfo_->text().contains("파일 없음"),
      "two-column Excel HTML and text clipboard imports both labelled files");
    // Also accept rows flattened into label newline filename pairs.
    window.parsePaths({});
    window.fileNames_->setPlainText(QString("5B-TS\n%1\n5C\n%2").arg(tsName,ircName));
    window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({folder.filePath(tsName),folder.filePath(ircName)})
      &&window.structureLabels()==QStringList({"5B-TS","5C"})&&!window.fileNamesInfo_->text().contains("파일 없음"),
      "flattened two-line spreadsheet rows import without interpreting labels as filenames");
    return ok;
  }
  static bool orderedReactionInputs(const QString& fixture){
    bool ok=true;QTemporaryDir folder;MainWindow window;window.baseDirectory_->setText(folder.path());
    const QString stem="3-CoIII-reaction-scan-freq-ts-freq";
    const QString rc="3-CoIII-previous-scan-freq-ts-freq-ircfwd-opt-freq.out";
    const QString ts=stem+".out",pc=stem+"-ircfwd-opt-freq.out",extra=stem+"-ircrev-opt-freq.out";
    for(const auto& name:{rc,ts,pc,extra})if(!QFile::copy(fixture,folder.filePath(name)))return false;
    const auto input=QString("3B\t%1\n3B-TS\t%2\n3C\t%3").arg(rc,ts,pc);
    window.fileNames_->setPlainText(input);window.addNamedFiles();
    const QStringList paths={folder.filePath(rc),folder.filePath(ts),folder.filePath(pc)};
    const QStringList labels={"3B","3B-TS","3C"};
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==labels,"RC TS PC spreadsheet order stays exact even when both IRC endpoints exist on disk");
    window.parsePaths(paths,true);ok&=check(waitForImport(window)&&window.sourcePaths()==paths,"multi-file explicit selection also keeps exactly selected files without extra IRC");
    window.refreshDocumentModel();const auto* spin=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(spin&&spin->rows.size()==3&&spin->rows[0][0]==labels[0]&&spin->rows[1][0]==labels[1]&&spin->rows[2][0]==labels[2],"spin table has only the three selected labelled structures, without unnamed reverse IRC");
    QJsonArray oldOrder{paths[1],paths[0],paths[2]};
    FigureSetupDialog dialog(window.documentModel_.coordinates,paths,QJsonObject{{"order",oldOrder}});dialog.findChild<QPushButton*>("nextFigureGroup")->click();
    ok&=check(dialog.figures()[0].structureNames==QStringList({"^{3}B","^{3}B-TS","^{3}C"}),"IRC filenames containing ts never cause RC and TS to swap, and legacy erroneous saved order is reset");
    window.spinNames_->setPlainText("old-A\nold-B");window.refreshDocumentModel();spin=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(!window.preflightErrors().isEmpty()&&spin&&spin->rows[0][0]=="3B"&&spin->rows[2][0]=="3C","partial spin names cannot silently mix manual and source names");
    window.spinNames_->clear();window.spinIndices_->setPlainText("1\n1");window.refreshDocumentModel();
    ok&=check(!window.preflightErrors().isEmpty()&&!window.documentModel_.table(si::DocumentSection::SpinDensity),"incomplete per-structure spin ID mapping never reuses the last row silently");window.spinIndices_->clear();
    // Repair a previously contaminated list through the explicit replacement action.
    window.parsePaths({folder.filePath(extra)},true);if(!waitForImport(window))return false;
    window.fileNames_->setPlainText(input);window.addNamedFiles(true);
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==labels,"replace from pasted list removes legacy auto-added IRC and restores exact order and labels");
    window.energyBasis_->setCurrentIndex(window.energyBasis_->findData(static_cast<int>(si::EnergyBasis::Electronic)));
    window.referencesText_->clear();window.computationalDetails_->clear();window.detailsEdited_=true;
    window.siOutputPath_->setText(folder.filePath("empty-details.docx"));window.xyzOutputPath_->setText(folder.filePath("empty-details.xyz"));
    chooseSiRendering(&window,QMessageBox::YesRole);
    window.exportSiDocument();
    ok&=check(waitUntil([&]{return QFile::exists(window.siOutputPath_->text())&&window.status_->text().contains("생성 완료");}),"SI export succeeds with both computational details and references empty");
    return ok;
  }
  static bool numberedReactionLabels(const QString& fixture){
    MainWindow window;QTemporaryDir folder;bool ok=true;
    QStringList rows,paths,labels;
    auto add=[&](const QString& label,int multiplicity,const QString& reaction,const QString& endpoint){
      const auto name=QString("%1-CoIII-Me3-TPADP-prtonated-for%2-scan-freq-ts-freq%3.out").arg(multiplicity).arg(reaction,endpoint);
      labels.append(label);paths.append(folder.filePath(name));rows.append(label+"\t"+name);
      return QFile::copy(fixture,paths.back());
    };
    const QString fwd="-ircfwd-opt-freq",rev="-ircrev-opt-freq";
    if(!add("1AP1",1,"Ring_Opening",fwd)||!add("1AP1-TS",1,"Ring_Opening",{})||!add("1BP1",1,"Ring_Opening",rev)||
       !add("1BP12",1,"OO_Cleavage",rev)||!add("1BP1-TS",1,"OO_Cleavage",{})||!add("1CP1-2",1,"OO_Cleavage",fwd)||
       !add("5BP12",5,"OO_Cleavage",fwd)||!add("5BP1-TS",5,"OO_Cleavage",{})||!add("5CP1-2",5,"OO_Cleavage",rev)||
       !add("3AP1",3,"Ring_Opening",fwd)||!add("3AP1-TS",3,"Ring_Opening",{})||!add("3BP1",3,"Ring_Opening",rev)||
       !add("5AP1",5,"Ring_Opening",fwd)||!add("5AP1-TS",5,"Ring_Opening",{})||!add("5BP1",5,"Ring_Opening",rev))return false;
    window.baseDirectory_->setText(folder.path());window.fileNames_->clear();
    QApplication::clipboard()->setText(rows.join("\r\n"));window.findChild<QGroupBox*>("importControls")->setChecked(true);window.fileNames_->paste();
    window.findChild<QPushButton*>("replaceNamedFiles")->click();
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths&&window.structureLabels()==labels,"user's 15-row Excel replacement preserves every numbered/hyphenated label and exact file order");
    ok&=check(!window.fileNamesInfo_->text().contains("파일 없음"),"reaction labels are never reported as missing filenames");
    window.refreshDocumentModel();
    for(int i=0;i<labels.size();++i){
      ok&=check(window.reference_->itemText(i)==labels[i]&&window.documentModel_.coordinates[i].structureName==labels[i],"reaction labels reach reference selector and coordinate blocks");
    }
    for(const auto& table:window.documentModel_.tables)if(table.section==si::DocumentSection::RelativeEnergy||table.section==si::DocumentSection::AbsoluteEnergy||table.section==si::DocumentSection::SpinDensity){
      ok&=check(table.rows.size()==labels.size(),"reaction table contains exactly the pasted structures");
      for(int i=0;i<table.rows.size();++i)ok&=check(table.rows[i][0]==labels[i],"energy and spin rows retain every pasted reaction label");
    }
    ProjectState state;state.files=window.sourcePaths();state.structureNames=window.structureLabels().join('\n');QString error;
    ok&=check(ProjectStore::save(folder.filePath("numbered-labels.json"),state,&error),"save numbered reaction labels");
    ProjectState loaded;ok&=check(ProjectStore::load(folder.filePath("numbered-labels.json"),&loaded,&error)&&loaded.structureNames==labels.join('\n')&&loaded.files==paths,"numbered reaction labels and order survive project serialization");
    window.fileNames_->setPlainText("1-CP1-2\t"+QFileInfo(paths[5]).fileName());window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.files_->rowCount()==15&&window.structureLabels()[5]=="1-CP1-2","hyphens after multiplicity and between reaction indices also relabel existing structures without duplication");
    // Export itself must repair legacy filename labels without requiring
    // another Add/Replace click or resetting the user's current file order.
    for(int i=0;i<paths.size();++i)window.files_->item(i,0)->setText(QFileInfo(paths[i]).completeBaseName());
    window.files_->selectRow(14);window.moveSelectedStructure(0);
    auto orderedPaths=paths;orderedPaths.move(14,0);auto orderedLabels=labels;orderedLabels.move(14,0);
    const auto originalData=window.parsed_.value(paths[5]);
    window.fileNames_->setPlainText(rows.join('\n'));
    window.energyBasis_->setCurrentIndex(window.energyBasis_->findData(static_cast<int>(si::EnergyBasis::Electronic)));
    const auto output=folder.filePath("export-synced-labels.docx");window.siOutputPath_->setText(output);window.xyzOutputPath_->setText(folder.filePath("export-synced-labels.xyz"));
    chooseSiRendering(&window,QMessageBox::NoRole);
    window.findChild<QToolButton*>("generateSiButton")->click();
    ok&=check(waitUntil([&]{return QFile::exists(output)&&window.status_->text().contains("생성 완료");})&&window.structureLabels()==orderedLabels&&window.sourcePaths()==orderedPaths,"Generate SI automatically applies all 15 pasted labels while retaining user-selected order");
    ok&=check(window.parsed_.value(paths[5])==originalData&&!window.parsing_,"export name synchronization reuses parsed structures and their spin sources");
    QFile generated(output);
    if(generated.open(QIODevice::ReadOnly)){
      const auto bytes=generated.readAll();
      ok&=check(bytes.contains("P1</w:t>")&&bytes.contains("-2</w:t>")&&!bytes.contains("TPADP-prtonated-for"),"generated SI DOCX contains hyphenated reaction labels instead of original filenames");
    }else ok=false;
    return ok;
  }
  static bool realReactionInputs(const QString& directory){
    bool ok=true;MainWindow window;window.baseDirectory_->setText(directory);
    const QString stem="3-CoIII-Me3-TPADP-AfterCC-forOO_Cleavage-scan-freq-ts-freq";
    const QStringList names={stem+"-ircrev-opt-freq.out",stem+".out",stem+"-ircfwd-opt-freq.out"};
    const QStringList labels={"3B","3B-TS","3C"};QStringList rows,paths;
    for(int i=0;i<names.size();++i){paths.append(QDir(directory).filePath(names[i]));if(!QFile::exists(paths.back()))return check(false,"real reaction input exists");rows.append(labels[i]+"\t"+names[i]);}
    window.fileNames_->setPlainText(rows.join('\n'));window.addNamedFiles(true);
    ok&=check(waitUntil([&]{return !window.parsing_;},30000)&&window.sourcePaths()==paths&&window.structureLabels()==labels,"real Deformylation RC TS PC files retain exact selected order and labels");
    const auto records=window.energyRecords();if(records.size()!=3)return false;
    QStringList coIds;
    for(const auto& record:records){const auto it=std::find_if(record.frequency.finalGeometry.begin(),record.frequency.finalGeometry.end(),[](const auto& atom){return atom.atomicNumber==27;});if(it==record.frequency.finalGeometry.end())return false;coIds.append(QString::number(1+std::distance(record.frequency.finalGeometry.begin(),it)));}
    window.spinIndices_->setPlainText(coIds.join('\n'));window.refreshDocumentModel();const auto* table=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(table&&table->rows.size()==3,"real-file spin table has no unwanted reverse IRC rows");if(!table)return false;
    si::GaussianParser parser;
    for(int i=0;i<3;++i){
      const auto& result=records[i].frequency;
      ok&=check(result.spinSourcePath==std::filesystem::path((paths[i].chopped(4)+"-bb.out").toStdWString()),"real structure uses its corresponding bb spin source");
      const auto original=parser.parse(result.spinSourcePath);double total=0,co=0;for(const auto& spin:original.spinDensities){total+=spin.spin;if(spin.atomIndex==coIds[i].toInt())co=spin.spin;}
      ok&=check(table->rows[i][0]==labels[i]&&std::abs(table->rows[i][1].toDouble()-co)<0.00051&&std::abs(table->rows[i][2].toDouble()-(total-co))<0.00051&&std::abs(table->rows[i][3].toDouble()-total)<0.00051,"real Co, Rest and Sum spin values match independent reads of each original bb file");
    }
    FigureSetupDialog dialog(window.documentModel_.coordinates,paths,{});dialog.findChild<QPushButton*>("nextFigureGroup")->click();
    ok&=check(dialog.figures()[0].structureNames==QStringList({"^{3}B","^{3}B-TS","^{3}C"}),"real IRC filenames containing ts do not reorder the first rendering");
    return ok;
  }
  static bool referenceDrafts(){
    bool ok=true;const QString reference="[42] Previously supplied reference text.";
    {MainWindow first;first.referencesText_->setPlainText(reference);first.computationalDetails_->setPlainText("Edited computational conditions.");}
    {MainWindow second;ok&=check(second.referencesText_->toPlainText()==reference&&second.computationalDetails_->toPlainText()=="Edited computational conditions."&&second.detailsEdited_,"references and edited details survive reopening the application");
      second.referencesText_->clear();second.findChild<QPushButton*>("restoreReferenceDraft")->click();ok&=check(second.referencesText_->toPlainText()==reference,"cleared references can be restored from last nonempty input");
      second.findChild<QPushButton*>("restoreOriginalReferences")->click();ok&=check(second.referencesText_->toPlainText().contains("Becke")&&second.referencesText_->toPlainText().contains("Marenich")&&second.referencesText_->toPlainText().contains("Gaussian 09"),"original SI computation references are recoverable from embedded source text");}
    {MainWindow window;window.referencesText_->setPlainText(reference);
      auto choose=[&](bool accept,bool onlyBasis){
        QTimer::singleShot(0,&window,[&,accept,onlyBasis]{
          auto* dialog=window.findChild<QDialog*>("publishedReferenceDialog");
          if(!dialog)return;
          if(onlyBasis)for(auto* choice:dialog->findChildren<QCheckBox*>())choice->setChecked(choice->objectName()=="S27");
          if(accept)dialog->accept();else dialog->reject();
        });
        window.findChild<QPushButton*>("publishedReferenceLibrary")->click();
      };
      choose(false,false);ok&=check(window.referencesText_->toPlainText()==reference,"published reference dialog cancellation preserves input");
      choose(true,true);const auto basis=window.referencesText_->toPlainText();
      ok&=check(basis.startsWith(reference)&&basis.contains("S27.")&&basis.contains("1057")&&!basis.contains("S25."),"selected def2 references append without replacing existing text");
      choose(true,false);const auto all=window.referencesText_->toPlainText();
      ok&=check(all.contains("S24.")&&all.contains("Revision B.01")&&all.contains("(e) Vosko")&&all.contains("S28.")&&all.contains("S29.")&&all.count("S27.")==1,"all six embedded references include split Gaussian author list and subreferences without duplication");
      choose(true,false);ok&=check(window.referencesText_->toPlainText()==all,"repeated library insertion is idempotent");
      window.refreshDocumentModel();ok&=check(window.documentModel_.referencesText==all,"published references propagate to SI document model");
    }
    return ok;
  }
  static bool namedAndTsImports(const QString& fixture){
    MainWindow window;QTemporaryDir folder;bool ok=true;
    const QString stem=QString::fromUtf8("반응 1-ts-freq");
    QStringList paths;
    for(const auto& suffix:{QString(),QStringLiteral("-ircfwd-opt-freq"),QStringLiteral("-ircrev-opt-freq")}){
      const auto path=folder.filePath(stem+suffix+".out");
      if(!QFile::copy(fixture,path))return false;paths.append(path);
      const auto bb=folder.filePath(stem+suffix+"-bb.out");
      if(!QFile::copy(fixture,bb))return false;
      QFile file(bb);if(!file.open(QIODevice::Append))return false;
      file.write(QString("\n Mulliken charges and spin densities:\n 1 O 0.0 %1\n\n").arg(0.8+paths.size()*0.01,0,'f',2).toUtf8());
    }
    const auto unrelated=folder.filePath("unrelated.out");QFile::copy(fixture,unrelated);
    QDir(folder.path()).mkdir("nested");QFile::copy(fixture,folder.filePath("nested/hidden.out"));
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(folder.path())});
    QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);window.dropEvent(&drop);
    ok&=check(window.baseDirectory_->text()==folder.path()&&window.sourcePaths().isEmpty()&&!window.parsing_,"folder drop only sets base folder and imports no direct or nested files");
    QApplication::clipboard()->setText(stem+"\t"+stem+".out\r\nmissing.out");
    window.findChild<QGroupBox*>("importControls")->setChecked(true);window.fileNames_->paste();
    auto* add=window.findChild<QPushButton*>("addNamedFiles");if(!add)return false;window.findChild<QGroupBox*>("importControls")->setChecked(true);add->click();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList{paths[0]},"explicit filename list does not discover unselected IRC structures");
    window.parsePaths({});window.parsePaths({paths[0]},true);
    ok&=check(waitForImport(window)&&window.sourcePaths()==paths,"direct TS file import still discovers its two endpoints");
    ok&=check(window.fileNamesInfo_->text().contains("missing.out"),"missing pasted filenames stay visible after parsing");
    const auto records=window.energyRecords();
    ok&=check(records.size()==3,"all linked structures reach energy records");
    for(int i=0;i<static_cast<int>(records.size());++i){
      const auto& result=records[i].frequency;
      ok&=check(result.spinSourcePath==std::filesystem::path((paths[i].chopped(4)+"-bb.out").toStdWString())&&result.spinDensities.size()==1&&std::abs(result.spinDensities[0].spin-(0.81+i*0.01))<1e-9&&!result.spinFallback,"each TS/IRC structure uses its own bb spin density");
      ok&=check(result.finalGeometry.size()==3&&records[i].singlePoint.has_value(),"linked structures retain frequency geometry and paired SPE");
    }
    window.files_->selectRow(2);window.findChild<QPushButton*>("moveStructureUp")->click();
    QStringList ordered{paths[0],paths[2],paths[1]};
    ok&=check(window.sourcePaths()==ordered,"up button controls automatically linked structure order");
    window.parsePaths(ordered);
    ok&=check(waitForImport(window)&&window.sourcePaths()==ordered,"project restoration retains reordered TS endpoints");
    window.parsePaths({paths[0]});
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList{paths[0]},"project restoration does not discover unlisted endpoints");
    // File drop uses the same linking path, including a missing endpoint.
    QFile::remove(paths[2]);window.parsePaths({});
    QMimeData fileMime;fileMime.setUrls({QUrl::fromLocalFile(paths[0])});
    QDropEvent fileDrop(QPointF(20,20),Qt::CopyAction,&fileMime,Qt::LeftButton,Qt::NoModifier);window.dropEvent(&fileDrop);
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({paths[0],paths[1]})&&window.status_->text().contains("ircrev-opt-freq.out"),"TS file drop imports existing endpoints and reports missing reverse file");
    window.fileNames_->setPlainText("unrelated.out");add->click();
    ok&=check(waitForImport(window)&&window.sourcePaths().back()==unrelated,"ordinary pasted names append without triggering unrelated discovery");
    const auto log=folder.filePath("unrelated.log");QFile::copy(fixture,log);
    const auto before=window.sourcePaths();window.fileNames_->setPlainText("unrelated");add->click();
    ok&=check(!window.parsing_&&window.sourcePaths()==before&&window.fileNamesInfo_->text().contains(".out/.log"),"ambiguous extensionless names require an explicit extension");
    return ok;
  }
  static bool appendFiles(const QString& fixture){
    MainWindow window;QTemporaryDir folder;bool ok=true;
    QStringList paths;
    for(const auto& name:{"first.log","second.log","third.log","fourth.log"}){
      const auto path=folder.filePath(name);ok&=check(QFile::copy(fixture,path),"create append fixture");paths.append(path);
    }
    window.parsePaths({paths[0],paths[1]},true);
    ok&=check(waitUntil([&]{return !window.parsing_;}),"initial additive import");
    window.files_->selectRow(1);
    window.files_->item(0,0)->setText("Custom first");window.syncReferenceOptions();window.reference_->setCurrentIndex(1);
    window.spinNames_->setPlainText("spin-first\nspin-second");
    window.spinIndices_->setPlainText("1\n2");
    window.geometryNames_->setPlainText("geo-first\ngeo-second");
    window.captions_->setPlainText("caption-first\ncaption-second");
    const auto originalData=window.parsed_.value(paths[0]);
    // The second request arrives while the first request is still parsing.
    window.parsePaths({paths[2]},true);
    window.parsePaths({paths[3],paths[2]},true);
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==paths,"queued additions append in order without replacing or duplicating existing files");
    ok&=check(window.files_->item(0,0)->text()=="Custom first"&&window.files_->currentRow()==1&&window.reference_->currentData().toString()==paths[1]&&window.parsed_.value(paths[0])==originalData,"append preserves custom labels, selected structure, reference and parsed data");
    ok&=check(window.spinNames_->toPlainText()=="spin-first\nspin-second"&&window.spinIndices_->toPlainText()=="1\n2"&&window.geometryNames_->toPlainText()=="geo-first\ngeo-second"&&window.captions_->toPlainText()=="caption-first\ncaption-second","append preserves per-structure inputs");
    window.parsePaths({paths[0],paths[0].toUpper()},true);
    window.parsePaths({},true);
    const auto auxiliary=folder.filePath("orphan-freq-bb.out");QFile::copy(fixture,auxiliary);
    window.parsePaths({auxiliary,folder.filePath("missing.log")},true);
    ok&=check(!window.parsing_&&window.sourcePaths()==paths,"duplicate, empty and auxiliary-only additions leave existing files intact");
    const auto dropped=folder.filePath("dropped.log");QFile::copy(fixture,dropped);
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(dropped)});
    QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&enter);
    QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&drop);
    auto expected=paths;expected.append(dropped);
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==expected,"drag-and-drop appends to the existing list");
    window.refreshDocumentModel();
    ok&=check(window.energyRecords().size()==5&&window.documentModel_.coordinates.size()==5,"all accumulated files reach the document model");
    // Project loading continues to use replacement semantics.
    window.parsePaths({paths[1]});
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==QStringList{paths[1]},"project import still replaces the current list");
    window.parsePaths({});
    ok&=check(window.sourcePaths().isEmpty()&&window.parsed_.isEmpty(),"empty project clears old structures");
    return ok;
  }
  static bool run(MainWindow& window){
    bool ok=true;QTemporaryDir folder;
    window.parsePaths({});
    auto write=[&](const QString& name,const QString& text){QFile file(folder.filePath(name));if(!file.open(QIODevice::WriteOnly))return false;return file.write(text.toUtf8())>=0;};
    auto fixture=[](int index,bool spe){
      const auto energy=spe?-101.0-index*0.2:-100.0-index*0.1;
      return QString(" Gaussian 16, Revision C.01\n # %1 UB3LYP/%2 scrf=(smd,solvent=water)\n\n"
        " Charge = 0 Multiplicity = %3\n SCF Done: E(UB3LYP) = %4 A.U.\n"
        " Standard orientation:\n -----\n Center Atomic Atomic Coordinates\n Number Number Type X Y Z\n -----\n"
        " 1 6 0 %5 0.000000 0.000000\n -----\n"
        " Mulliken charges and spin densities:\n 1 C 0.000 %6\n 2 H 0.000 0.2\n"
        " Sum of Mulliken charges = 0.0\n APT charges:\n 1 C 0.0 9.0\n\n"
        " Temperature 298.150 Kelvin. Pressure 1.0 Atm.\n S**2 before annihilation 2.01, after 2.00\n")
        .arg(spe?"SP":"opt freq",spe?"Def2-TZVPP":"Def2-SVP").arg(index?3:1).arg(energy,0,'f',8).arg(spe?"9.876543":"1.234567").arg(spe?"0.8":"0.2")
        +(spe?QString():QString(" Frequencies -- %1 100.0 200.0\n Zero-point correction= 0.03\n Thermal correction to Energy= 0.04\n Thermal correction to Gibbs Free Energy= %2\n Sum of electronic and thermal Free Energies= %3\n")
          .arg(index==2?"-123.45":"50.0").arg(0.02+index*0.01,0,'f',8).arg(energy+0.02+index*0.01,0,'f',8))
        +(spe?QString():QString(" Zero-point vibrational energy 78769.0 (Joules/Mol)\n %1 (Kcal/Mol)\n E (Thermal) CV S\n KCal/Mol Cal/Mol-Kelvin Cal/Mol-Kelvin\n Total %2 10.000 %3\n Dispersion energy = %4\n")
          .arg(18.826+index*0.3,0,'f',3).arg(25.100+index*0.5,0,'f',3).arg(50.0+index*5,0,'f',3).arg(-0.010+index*0.002,0,'f',8))
        +" Normal termination of Gaussian 16\n";
    };
    const QStringList names={"1A-freq","3A-freq","3TS-freq"};
    for(int i=0;i<names.size();++i){ok&=check(write(names[i]+".out",fixture(i,false)),"write primary fixture");ok&=check(write(names[i]+"-bb.out",fixture(i,true)),"write SPE fixture");}
    write("orphan-freq-bb.out",fixture(0,true));
    QList<QUrl> urls{QUrl::fromLocalFile(folder.path())};
    for(const auto& name:names){urls.append(QUrl::fromLocalFile(folder.filePath(name+".out")));urls.append(QUrl::fromLocalFile(folder.filePath(name+"-bb.out")));}
    urls.append(QUrl::fromLocalFile(folder.filePath("orphan-freq-bb.out")));
    QMimeData mime;mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&enter);
    QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&drop);
    ok&=check(waitUntil([&]{return !window.parsing_&&window.files_->rowCount()==3;}),"explicit file drop with folder: six paired files plus orphan produce three records");
    window.refreshDocumentModel();
    ok&=check(window.files_->columnCount()==7&&window.reference_->count()==3,"preflight columns and reference dropdown");
    for(int row=0;row<window.files_->rowCount();++row)ok&=check(window.files_->item(row,6)->text()=="Ready","complete pair Ready");
    const auto records=window.energyRecords();
    ok&=check(records.size()==3&&records.front().singlePoint&&records.front().frequency.spinDensities.front().spin==0.8&&records.front().frequency.finalGeometry.front().xyz[0]==1.234567,"SPE energy/spin and primary geometry roles");
    ok&=check(!window.documentModel_.table(si::DocumentSection::Validation),"legacy SI excludes validation table");
    window.spinIndices_->setPlainText("1");window.refreshDocumentModel();
    const auto* spinTable=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(spinTable&&spinTable->rows.front()==QStringList({"1A-freq","0.800","0.200","1.000"}),"Rest sums only unselected atoms from one Mulliken block");
    window.spinIndices_->setPlainText("1 1");window.refreshDocumentModel();
    spinTable=window.documentModel_.table(si::DocumentSection::SpinDensity);
    ok&=check(spinTable&&spinTable->rows.front().at(3)=="0.200"&&!window.preflightErrors().isEmpty(),"duplicate spin IDs do not double-subtract Rest and block export");
    window.spinIndices_->setPlainText("1");window.refreshDocumentModel();
    auto* selector=window.preview_->findChild<QComboBox*>("previewTableSelector");
    auto* copy=window.preview_->findChild<QPushButton*>("copyPreviewTableButton");
    auto* copyImage=window.preview_->findChild<QPushButton*>("copyPreviewTableImageButton");
    auto* browser=window.preview_->findChild<QTextBrowser*>();
    ok&=check(selector&&copy&&copyImage&&browser&&selector->count()==window.documentModel_.tables.size()+1&&!copy->isEnabled()&&!copyImage->isEnabled(),"table selector lists every table and full-document copy disabled");
    if(selector&&copy&&copyImage&&browser){
      for(int i=0;i<window.documentModel_.tables.size();++i){
        selector->setCurrentIndex(i+1);QCoreApplication::processEvents();
        const auto expected=window.documentModel_.tables[i];
        ok&=check(copy->isEnabled()&&browser->toPlainText().contains(QString("Table S%1.").arg(i+1))&&!browser->toPlainText().contains("Cartesian Coordinates"),"selected table preview isolated from document");
        copy->click();const auto* mime=QApplication::clipboard()->mimeData();
        QStringList rows{expected.headers.join('\t')};for(const auto& row:expected.rows)rows.append(row.join('\t'));
        ok&=check(mime&&mime->hasHtml()&&mime->text()==rows.join('\n'),"clipboard has exact TSV cells and HTML table");
        QTextDocument pasted;pasted.setHtml(mime->html());
        ok&=check(pasted.toPlainText().contains(expected.rows.front().front())&&mime->html().contains("<table"),"rich HTML paste retains table data");
        ok&=check(copyImage->isEnabled(),"image copy enabled for selected table");
        clickClipboardButton(copyImage);
        const auto* imageMime=QApplication::clipboard()->mimeData();
        const QImage image=QImage::fromData(imageMime->data("image/png"),"PNG");
        ok&=check(imageMime->hasImage()&&!imageMime->hasHtml()&&!image.isNull()&&image.hasAlphaChannel(),"clipboard provides image and transparent PNG");
        bool transparent=false,ink=false,blackOnly=true;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
          const auto pixel=image.pixelColor(x,y);
          transparent|=pixel.alpha()==0;ink|=pixel.alpha()>0;
          if(pixel.alpha()>0)blackOnly&=pixel.red()==0&&pixel.green()==0&&pixel.blue()==0;
        }
        ok&=check(transparent&&ink&&blackOnly,"table image contains black ink on transparent background without white fill");
        // Viewport size and scroll must not change the exported table.
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
        clickClipboardButton(copyImage);
        const QImage scrolledImage=QImage::fromData(QApplication::clipboard()->mimeData()->data("image/png"),"PNG");
        ok&=check(scrolledImage==image,"image copy independent of preview scroll");
        window.refreshDocumentModel();ok&=check(selector->currentIndex()==i+1,"table choice retained after document refresh");
      }
      selector->setCurrentIndex(0);
      ok&=check(!copyImage->isEnabled(),"image copy disabled for full document");
    }
    window.spinIndices_->clear();window.refreshDocumentModel();
    const auto ref=window.reference_->findText("1A-freq");ok&=check(ref>=0,"structure label selection");
    window.reference_->setCurrentIndex(ref);window.refreshDocumentModel();
    window.includeAbsolute_->setChecked(true);window.refreshDocumentModel();
    const auto* absolute=window.documentModel_.table(si::DocumentSection::AbsoluteEnergy);
    ok&=check(window.energyBasis_->currentData().toInt()==static_cast<int>(si::EnergyBasis::LegacyComposite),"original Python energy mode selected by default");
    ok&=check(absolute&&absolute->headers==QStringList({"","E_{Def2-SVP}","E_{Def2-TZVPP}","Z_{0}","E_{Thermal}","S","Dispersion"})&&
      absolute->rows.front()==QStringList({"1A-freq","-100.00","-101.00","18.83","25.10","50.00","-0.01"}),"legacy absolute columns and Gaussian printed kcal values");
    const auto* relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    window.includeDispersion_->setChecked(true);window.includeComplexation_->setChecked(true);window.refreshDocumentModel();relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    const QStringList expectedRelative{"3A-freq","-62.75","-62.75","-125.50","+0.30","+0.20","-1.49","+1.26","+0.00","-125.24"};
    ok&=check(relative&&relative->headers.size()==10&&relative->rows[1]==expectedRelative&&relative->boldColumns==QVector<int>({3,9}),"restored nine-value relative table with Python Excel formulas and signs");
    for(bool dispersion:{false,true})for(bool complexation:{false,true}){
      window.includeDispersion_->setChecked(dispersion);window.includeComplexation_->setChecked(complexation);window.refreshDocumentModel();
      relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
      ok&=check(relative&&relative->headers.size()==8+int(dispersion)+int(complexation)&&relative->headers.contains("Δ_{Dispersion}")==dispersion&&relative->headers.contains("Δ_{Complexation}")==complexation,"automatic legacy optional columns");
      const double expected=-0.2*si::HartreeToKcal+0.5-1.49075+(dispersion?0.002*si::HartreeToKcal:0.0);
      ok&=check(relative&&relative->rows[1].back()==si::formatNumber(expected,2,0),"relative Gibbs includes only enabled correction terms");
      if(relative){
        DocumentPreviewWidget checkedPreview;si::SIDocumentModel checkedModel;checkedModel.tables.append(*relative);checkedPreview.setDocumentModel(checkedModel);
        const QString word=dispersion&&complexation?"six":dispersion||complexation?"five":"four";
        ok&=check(checkedPreview.findChild<QTextBrowser*>()->toPlainText().contains(QString("sum of the previous %1 columns").arg(word)),"preview footnote agrees with selected dispersion and complexation terms");
      }
    }
    window.includeDispersion_->setChecked(true);window.includeComplexation_->setChecked(true);
    window.energyBasis_->setCurrentIndex(window.energyBasis_->findData(static_cast<int>(si::EnergyBasis::Electronic)));
    window.referenceMode_->setCurrentIndex(window.referenceMode_->findData("manual_numeric"));window.manualReference_->setText("-101");
    window.refreshDocumentModel();relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    ok&=check(relative&&relative->headers.size()==10&&relative->rows[1].back()==si::formatNumber(-0.2*si::HartreeToKcal,2,0),"manual numeric reference keeps restored table and respects energy basis");
    window.referenceMode_->setCurrentIndex(window.referenceMode_->findData("single"));
    window.energyBasis_->setCurrentIndex(0);window.computationalDetails_->setPlainText("User-edited computational details.");
    window.manuscriptTitle_->setText("Phase 1 smoke manuscript");window.refreshDocumentModel();
    ok&=check(window.documentModel_.computationalDetails=="User-edited computational details.","user details preserved after refresh");
    const auto originalPaths=window.sourcePaths();const auto referencePath=window.reference_->currentData().toString();
    window.spinNames_->setPlainText("spin-A\nspin-B\nspin-C");window.spinIndices_->setPlainText("1|2|1 2");
    window.geometryNames_->setPlainText("geo-A\ngeo-B\ngeo-C");window.captions_->setPlainText("figure-A\nfigure-B\nfigure-C");
    window.files_->selectRow(2);window.moveSelectedStructure(0);
    const QStringList orderedPaths{originalPaths[2],originalPaths[0],originalPaths[1]};
    ok&=check(window.sourcePaths()==orderedPaths&&window.files_->currentRow()==0&&window.reference_->currentData().toString()==referencePath,"move to arbitrary position preserves selected structure and reference path");
    ok&=check(window.spinNames_->toPlainText()=="spin-C\nspin-A\nspin-B"&&window.spinIndices_->toPlainText()=="1 2\n1\n2"&&window.geometryNames_->toPlainText()=="geo-C\ngeo-A\ngeo-B"&&window.captions_->toPlainText()=="figure-A\nfigure-B\nfigure-C","per-structure inputs move with files");
    window.spinIndices_->clear();window.refreshDocumentModel();
    for(const auto& table:window.documentModel_.tables)ok&=check(table.rows.front().front()==(table.section==si::DocumentSection::SpinDensity?"spin-C":"3TS-freq"),"all generated table rows follow file order");
    ok&=check(window.documentModel_.coordinates.front().structureName=="3TS-freq","coordinate order follows files");
    auto* up=window.findChild<QPushButton*>("moveStructureUp");auto* down=window.findChild<QPushButton*>("moveStructureDown");
    ok&=check(up&&down&&!up->isEnabled()&&down->isEnabled(),"reordering button boundaries");
    if(up&&down){down->click();up->click();ok&=check(window.sourcePaths()==orderedPaths,"up/down controls move the selected file");}
    ProjectState state;state.files=window.sourcePaths();state.structureNames=window.structureLabels().join('\n');state.referencePath=referencePath;
    ProjectState restored;QString orderError;
    ok&=check(ProjectStore::save(folder.filePath("ordered.json"),state,&orderError)&&ProjectStore::load(folder.filePath("ordered.json"),&restored,&orderError)&&restored.files==orderedPaths,"project serialization retains custom file order");
    window.pendingReferencePath_=restored.referencePath;window.parsePaths(restored.files);
    ok&=check(waitUntil([&]{return !window.parsing_;})&&window.sourcePaths()==orderedPaths&&window.reference_->currentData().toString()==referencePath,"project reparse retains saved order without alphabetical resorting");
    window.spinNames_->clear();window.geometryNames_->clear();window.captions_->clear();window.refreshDocumentModel();
    ok&=check(si::XyzWriter::write(folder.filePath("ordered.xyz"),window.documentModel_,&orderError),"ordered XYZ write");
    QFile orderedXyz(folder.filePath("ordered.xyz"));
    ok&=check(orderedXyz.open(QIODevice::ReadOnly)&&orderedXyz.readAll().replace("\r\n","\n").startsWith("1\n^{3}TS-freq"),"XYZ export uses custom file order");
    ok&=check(DocxWriter::writeDocument(folder.filePath("ordered.docx"),window.documentModel_,&orderError),"ordered DOCX write");
    QFile orderedDocx(folder.filePath("ordered.docx"));
    if(orderedDocx.open(QIODevice::ReadOnly)){const auto bytes=orderedDocx.readAll();auto visible=QString::fromUtf8(bytes);visible.remove(QRegularExpression("<[^>]*>"));ok&=check(visible.indexOf("3TS-freq")>=0&&visible.indexOf("3TS-freq")<visible.indexOf("1A-freq")&&visible.indexOf("1A-freq")<visible.indexOf("3A-freq"),"DOCX table rows use custom file order");}else ok=false;
    window.files_->selectRow(0);window.moveSelectedStructure(2);window.refreshDocumentModel();
    window.referencesText_->setPlainText("[1] SI output test reference.");
    window.siOutputPath_->setText(folder.filePath("SI.docx"));window.xyzOutputPath_->setText(folder.filePath("SI.xyz"));
    chooseSiRendering(&window,QMessageBox::YesRole);
    window.exportSiDocument();
    ok&=check(waitUntil([&]{return window.status_->text().contains("생성 완료")&&window.status_->text().contains("SI.docx");}),"SI export UI flow");
    ok&=check(!window.documentModel_.figures.isEmpty(),"rendering opt in retains molecular figures");
    window.captions_->setPlainText("Previously prepared Figure caption");window.refreshDocumentModel();
    window.siOutputPath_->setText(folder.filePath("tables-only.docx"));
    chooseSiRendering(&window,QMessageBox::NoRole);window.exportSiDocument();
    ok&=check(waitUntil([&]{return window.status_->text().contains("생성 완료")&&window.status_->text().contains("tables-only.docx");}),"table-only SI export skips figure setup");
    QFile tablesOnly(folder.filePath("tables-only.docx"));
    if(tablesOnly.open(QIODevice::ReadOnly)){
      const auto bytes=tablesOnly.readAll();
      ok&=check(bytes.contains("<w:tbl>")&&!bytes.contains("word/media/figure")&&!bytes.contains("<w:drawing>")&&!bytes.contains("Figure_S")&&!bytes.contains("Previously prepared Figure caption"),"table-only DOCX contains tables without cached figures or captions");
      ok&=check(bytes.contains("Cartesian Coordinates"),"table-only SI preserves coordinate data");
    }else ok=false;
    ok&=check(!window.preparedFigures_.isEmpty()&&window.captions_->toPlainText()=="Previously prepared Figure caption","table-only export preserves prepared figures and caption input");
    window.siOutputPath_->setText(folder.filePath("cancelled.docx"));
    chooseSiRendering(&window,QMessageBox::RejectRole);window.exportSiDocument();
    ok&=check(!QFile::exists(window.siOutputPath_->text()),"cancel rendering choice does not create SI");
    window.xyzOutputPath_->setText(folder.filePath("Structures.xyz"));window.exportXyzDocument();
    QFile xyz(folder.filePath("Structures.xyz"));ok&=check(xyz.open(QIODevice::ReadOnly)&&xyz.readAll().replace("\r\n","\n").startsWith("1\n^{1}A-freq"),"Generate XYZ produces text");
    QString error;ok&=check(DocxWriter::writeCoordinates(folder.filePath("Coordinates.docx"),window.documentModel_,&error),"independent coordinates DOCX");

    window.parsePaths({folder.filePath("orphan-freq-bb.out")});
    ok&=check(window.files_->rowCount()==0,"standalone BB excluded");
    write("missing-freq.out",fixture(0,false));window.parsePaths({folder.filePath("missing-freq.out")});
    ok&=check(waitUntil([&]{return !window.parsing_;}),"missing SPE import");window.refreshDocumentModel();
    ok&=check(window.files_->item(0,6)->text()=="Warning"&&window.files_->item(0,6)->toolTip().contains("SPE missing")&&window.preflightErrors().isEmpty(),"missing SPE warns but allows export");
    absolute=window.documentModel_.table(si::DocumentSection::AbsoluteEnergy);
    ok&=check(absolute&&absolute->rows.front()[2]==absolute->rows.front()[1],"missing SPE uses frequency energy fallback as in Python");

    auto bad=fixture(0,false);bad.replace(" Frequencies -- 50.0"," Frequencies -- -50.0 -60.0");
    write("mismatch-freq.out",bad);auto mismatch=fixture(0,true);mismatch.replace("Charge = 0 Multiplicity = 1","Charge = 1 Multiplicity = 3");write("mismatch-freq-bb.out",mismatch);
    window.parsePaths({folder.filePath("mismatch-freq.out")});ok&=check(waitUntil([&]{return !window.parsing_;}),"mismatch import");window.refreshDocumentModel();
    ok&=check(window.files_->item(0,6)->text()=="Warning"&&window.files_->item(0,6)->toolTip().contains("charge mismatch")&&window.files_->item(0,6)->toolTip().contains("multiplicity mismatch")&&window.files_->item(0,6)->toolTip().contains("2+ imaginary"),"pair mismatch and imaginary warnings");

    write("broken-freq.out","not a Gaussian output\n");window.parsePaths({folder.filePath("broken-freq.out")});
    ok&=check(waitUntil([&]{return !window.parsing_;}),"invalid input import");window.refreshDocumentModel();
    ok&=check(window.files_->item(0,6)->text()=="Error"&&!window.preflightErrors().isEmpty()&&window.preflightErrors().join(' ').contains("broken-freq"),"invalid structure blocks export with named error");
    window.files_->setRowCount(0);
    window.energyInputMode_->setCurrentIndex(window.energyInputMode_->findData("manual"));
    window.includeAbsolute_->setChecked(false);
    window.manualRelativeNames_->setPlainText("^{1}TS_{E}");
    for(bool dispersion:{false,true})for(bool complexation:{false,true}){
      window.includeDispersion_->setChecked(dispersion);window.includeComplexation_->setChecked(complexation);
      QStringList values{"-51.01","+5.00","-46.01","+0.39","-1.04","+2.58"};
      if(dispersion)values.append("+0.12");
      if(complexation)values.append("-0.34");
      values.append("-44.08");
      window.manualRelativeValues_->setPlainText(values.join('\t'));
      window.refreshDocumentModel();
      const auto* manual=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
      ok&=check(window.preflightErrors().isEmpty()&&manual&&manual->rows.front().mid(1)==values,"manual selected columns preserve all signed input values including Gibbs");
      ok&=check(manual&&manual->headers.contains("Δ_{Dispersion}")==dispersion&&manual->headers.contains("Δ_{Complexation}")==complexation,"manual optional columns independently selected");
      ok&=check(manual&&manual->boldColumns==QVector<int>({3,static_cast<int>(manual->headers.size())-1}),"manual Total and Gibbs bold columns");
      QString error;
      ok&=check(DocxWriter::writeDocument(QDir::current().filePath(QString("manual-%1-%2.docx").arg(dispersion).arg(complexation)),window.documentModel_,&error),"manual DOCX export");
    }
    window.includeDispersion_->setChecked(false);window.includeComplexation_->setChecked(false);
    window.manualRelativeValues_->setPlainText("-51.01 +5.00 -46.01 +0.39 -1.04 +2.58 +0.12 -0.34 -44.08");
    window.refreshDocumentModel();
    relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    ok&=check(window.preflightErrors().isEmpty()&&relative&&relative->rows.front().back()=="-44.08"&&relative->headers.size()==8,"legacy full nine-column manual input remains supported");
    window.manualRelativeValues_->setPlainText("-51.01 +5.00");
    ok&=check(!window.preflightErrors().isEmpty(),"incomplete manual row blocked before export");
    return ok;
  }
};

bool figureWorkflowSmoke(){
  bool ok=true;
  QVector<si::CoordinateBlock> structures;QStringList paths;
  const std::vector<si::Atom> geometry={{6,"C",{0,0,0}},{6,"C",{1.4,0,0}},{8,"O",{2.1,1.1,0}},{7,"N",{-0.7,1.2,0.3}},{1,"H",{-0.4,-0.8,0}},{1,"H",{1.9,-0.8,0.3}},{1,"H",{-1.4,1.3,0.8}}};
  for(int i=0;i<7;++i){auto atoms=geometry;atoms[2].xyz[2]=i*0.12;structures.append({QString("Structure %1").arg(i+1),atoms});paths.append(QString("structure-%1.out").arg(i));}
  auto renderLabel=[&](const QString& name){auto structure=structures.front();structure.structureName=name;FigureSetupDialog labelDialog({structure},{paths.front()},{});labelDialog.findChild<QPushButton*>("nextFigureGroup")->click();const auto png=labelDialog.figures().front().png;if(name=="5DCR-TS"&&qEnvironmentVariableIsSet("SI_FIGURE_SAMPLE_DIR")){const auto image=QImage::fromData(png);QImage white(image.size(),QImage::Format_RGB32);white.fill(Qt::white);QPainter painter(&white);painter.drawImage(0,0,image);painter.end();white.save(qEnvironmentVariable("SI_FIGURE_SAMPLE_DIR")+"/label-figure.png");}return png;};
  ok&=check(renderLabel("5DCR-TS")==renderLabel("^{5}D_{CR}-TS"),"Figure labels render plain shorthand and explicit super/subscript identically");
  for(const auto& example:QVector<QPair<QString,QString>>{{"1FCR2","^{1}F_{CR}2"},{"3ERC2","^{3}E_{RC}2"},{"1DP32","^{1}D_{P3}2"}})
    ok&=check(renderLabel(example.first)==renderLabel(example.second),"numbered CR/RC/P labels render with the same Figure subscripts as explicit markup");

  // Figure-only hydrogen filtering must preserve the original atom IDs and remove attached bonds.
  {
    FigureSetupDialog hydrogenDialog({structures.front()},{paths.front()},{});
    auto* hide=hydrogenDialog.findChild<QCheckBox*>("hideFigureHydrogens");
    auto* keep=hydrogenDialog.findChild<QComboBox*>("figureHydrogenKeep0");
    auto* viewer=hydrogenDialog.findChild<MoleculeViewWidget*>("figureView0");
    ok&=check(hide&&keep&&viewer&&keep->count()==4&&!hide->isChecked(),"Figure hydrogen controls offer all H indices and default to showing all");
    if(hide&&keep&&viewer){
      const auto all=viewer->renderMolecule(QSize(480,400));
      hide->setChecked(true);
      const auto none=viewer->renderMolecule(QSize(480,400));
      keep->setCurrentIndex(keep->findData(5));
      const auto retained=viewer->renderMolecule(QSize(480,400));
      ok&=check(all!=none&&none!=retained&&all!=retained,"all H, no H, and only H(5) render differently");
      auto expectedImage=[&](int count){
        MoleculeViewWidget expected;
        expected.setRenderStyle(viewer->renderStyle());
        expected.setMolecule(std::vector<si::Atom>(geometry.begin(),geometry.begin()+count),{});
        expected.setAtomIndicesVisible(false);
        expected.setCameraState(viewer->cameraState());
        return expected.renderMolecule(QSize(480,400));
      };
      ok&=check(none==expectedImage(4)&&retained==expectedImage(5),"hidden H and their bonds are removed without changing heavy atoms or retained H");
      const auto saved=hydrogenDialog.settings();
      ok&=check(saved.value("hide_hydrogens").toBool()&&saved.value("retained_hydrogens").toObject().value(paths.front()).toInt()==5,"C–H activation hydrogen selection persists by source path");
      hydrogenDialog.findChild<QPushButton*>("nextFigureGroup")->click();
      FigureSetupDialog restored({structures.front()},{paths.front()},saved);
      auto* restoredKeep=restored.findChild<QComboBox*>("figureHydrogenKeep0");
      ok&=check(restoredKeep&&restoredKeep->currentData().toInt()==5&&restored.findChild<QCheckBox*>("hideFigureHydrogens")->isChecked(),"H visibility restored with Figure settings");
      restored.findChild<QPushButton*>("nextFigureGroup")->click();
      ok&=check(hydrogenDialog.figures().front().png==restored.figures().front().png,"exported Figure PNG honors saved H visibility");
    }
  }

  FigureSetupDialog dialog(structures,paths,{});dialog.show();QTest::qWait(100);
  auto visibleViews=[&]{QVector<MoleculeViewWidget*> result;for(auto* view:dialog.findChildren<MoleculeViewWidget*>())if(view->isVisible())result.append(view);return result;};
  auto views=visibleViews();ok&=check(views.size()==3,"first Figure group has three molecules");
  auto state=views[1]->cameraState();state.insert("yaw",48.0);views[1]->setCameraState(state);
  ok&=check(views[0]->cameraState()==state&&views[2]->cameraState()==state,"TS camera is shared with endpoints");
  dialog.findChild<QCheckBox*>("linkFigureCameras")->setChecked(false);
  state.insert("roll",25.0);views[0]->setCameraState(state);
  ok&=check(views[1]->cameraState()!=state,"independent view adjustment");
  const auto style=views[0]->renderStyle();
  QSettings saved(QSettings::NativeFormat,QSettings::UserScope,"Youngcraft","Youngcraft");
  for(const int z:{1,6,7,8,26,27}){
    const auto savedColor=saved.value(QString("style/elementColors/%1").arg(z)).value<QColor>();
    if(savedColor.isValid())ok&=check(molecularElementColor(z,style)==savedColor,"exact saved Youngcraft element RGB imported");
  }
  ok&=check(style.value("bondColorMode").toInt()==saved.value("style/bondColorMode",0).toInt(),"saved Youngcraft bond color mode imported");
  if(saved.contains("style/ambientIntensity"))ok&=check(std::abs(style.value("ambientIntensity").toDouble()-saved.value("style/ambientIntensity").toDouble())<1e-7,"saved Youngcraft lighting imported");
  auto flat=style;flat.insert("lighting",false);const auto material=molecularSphere(molecularElementColor(8,style),flat);
  ok&=check(material.pixelColor(96,96)==molecularElementColor(8,style),"unlit material preserves source RGB without extra saturation or tint");
  const auto beforeImage=views[0]->renderMolecule(QSize(480,400));
  auto rotated=views[0]->cameraState();rotated.insert("yaw",rotated.value("yaw").toDouble()+50);views[0]->setCameraState(rotated);
  ok&=check(beforeImage!=views[0]->renderMolecule(QSize(480,400)),"manual camera angle changes rendered pixels");
  if(qEnvironmentVariableIsSet("SI_FIGURE_SAMPLE_DIR")){const auto directory=qEnvironmentVariable("SI_FIGURE_SAMPLE_DIR");QDir().mkpath(directory);dialog.grab().save(directory+"/figure-dialog.png");QFile json(directory+"/youngcraft-style.json");if(json.open(QIODevice::WriteOnly))json.write(QJsonDocument(style).toJson());}
  const auto independent=views[0]->cameraState();
  auto* next=dialog.findChild<QPushButton*>("nextFigureGroup");next->click();QTest::qWait(40);
  ok&=check(visibleViews().size()==2,"second group has two new molecules");
  dialog.findChild<QPushButton*>("previousFigureGroup")->click();QTest::qWait(40);
  ok&=check(visibleViews()[0]->cameraState()==independent,"previous preserves independently selected camera");
  auto* selector=dialog.findChild<QComboBox*>("figureStructure0");selector->setCurrentIndex(2);QTest::qWait(40);
  next->click();QTest::qWait(40);next->click();QTest::qWait(40);
  ok&=check(visibleViews().size()==2,"third group has two new molecules");
  next->click();QTest::qWait(40);
  const auto figures=dialog.figures();
  ok&=check(dialog.result()==QDialog::Accepted&&figures.size()==3,"all Figure groups rendered");
  ok&=check(figures[0].pixelWidth==3600&&figures[1].pixelWidth==2400&&figures[2].structureNames.size()==2,"3 then 2 then 2 Figure panel sizes");
  QSet<QString> names;for(const auto& figure:figures){for(const auto& name:figure.structureNames)names.insert(name);const auto image=QImage::fromData(figure.png);ok&=check(!image.isNull()&&qAlpha(image.pixel(0,0))==0,"self contained transparent molecular PNG");}
  ok&=check(names.size()==7&&figures[0].structureNames[0]=="Structure 3","manual endpoint swap preserves every molecule once");
  FigureSetupDialog customized(structures,paths,{});customized.show();
  QTimer::singleShot(0,&customized,[]{
    if(auto* chooser=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){
      if(auto* choices=chooser->findChild<QComboBox*>())choices->setCurrentIndex(5);chooser->accept();
    }
  });
  customized.findChild<QPushButton*>("appendFigureRow")->click();
  auto* customSelector=customized.findChild<QComboBox*>("figureStructure0");
  if(customSelector)customSelector->setCurrentIndex(5);
  ok&=check(customSelector&&customSelector->count()==7&&customSelector->currentData().toInt()==5,
    "custom Figure row allows choosing any named structure independently");
  QTimer::singleShot(0,&customized,[]{
    if(auto* chooser=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){
      if(auto* choices=chooser->findChild<QComboBox*>())choices->setCurrentIndex(0);chooser->accept();
    }
  });
  customized.findChild<QPushButton*>("addFigurePanel")->click();
  customized.findChild<QComboBox*>("figureStructure0")->setCurrentIndex(1);
  customized.findChild<QPushButton*>("nextFigureGroup")->click();
  ok&=check(customized.figures().size()==4&&customized.figures().last().structureNames==QStringList({"Structure 2","Structure 1"}),
    "custom Figure row has freely selected structures, with repeated use across rows");
  const auto customSettings=customized.settings();
  ok&=check(customSettings.value("rows").toArray().size()==4&&customSettings.value("rows").toArray().last().toArray().size()==2,
    "custom row composition is serialized for project persistence");
  FigureSetupDialog restoredFigure(structures,paths,customSettings);
  ok&=check(restoredFigure.settings().value("rows")==customSettings.value("rows"),
    "custom Figure rows are restored with their source identities");
  ProjectState project;project.files=paths;project.figureSettings=dialog.settings();QString error;QTemporaryDir temp;
  ok&=check(ProjectStore::save(temp.filePath("figure.json"),project,&error),"save Figure settings");ProjectState restored;
  ok&=check(ProjectStore::load(temp.filePath("figure.json"),&restored,&error)&&restored.figureSettings==project.figureSettings&&!restored.includeAbsolute,"camera/order settings roundtrip and absolute opt in");
  si::SIDocumentModel model;model.figures=figures;model.coordinates=structures;model.computationalDetails="Geometry optimizations and vibrational frequency calculations were performed using the calculation methods reported in the source files [1].";model.referencesText="[1] Test reference supplied for output verification.";
  for(const auto section:{si::DocumentSection::AbsoluteEnergy,si::DocumentSection::RelativeEnergy,si::DocumentSection::SpinDensity,si::DocumentSection::SelectedGeometry})model.tables.append({section,QString("Section %1").arg(int(section)),{"Structure","Value"},{{"Structure 1","0.000"}},{}});
  const auto output=QDir::current().filePath("si-figures-current-smoke.docx");ok&=check(DocxWriter::writeDocument(output,model,&error),"DOCX with embedded rendered molecule Figures");
  QFile docx(output);ok&=check(docx.open(QIODevice::ReadOnly),"read molecular DOCX");const auto bytes=docx.readAll();
  ok&=check(bytes.contains("word/media/figure1.png")&&bytes.contains("r:embed=\"rIdFigure3\"")&&bytes.contains("image/png"),"embedded media parts and DrawingML references");
  if(qEnvironmentVariableIsSet("SI_FIGURE_SAMPLE_DIR")){const auto directory=qEnvironmentVariable("SI_FIGURE_SAMPLE_DIR");QDir().mkpath(directory);const auto image=QImage::fromData(figures[0].png);image.save(directory+"/figure.png");QImage white(image.size(),QImage::Format_RGB32);white.fill(Qt::white);QPainter painter(&white);painter.drawImage(0,0,image);painter.end();white.save(directory+"/figure-white.png");}
  return ok;
}

int main(int argc,char** argv){
  qputenv("SI_GENERATOR_TEST_SETTINGS","1");
  QApplication app(argc,argv);bool unexpectedModal=false;QTimer modalGuard;modalGuard.setInterval(4000);QObject::connect(&modalGuard,&QTimer::timeout,&app,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){std::cerr<<"FAILED: unexpected modal: "<<box->text().toStdString()<<'\n';unexpectedModal=true;box->reject();}});modalGuard.start();{QSettings test("SI Generator","SIGeneratorTests");test.clear();}if(argc!=2)return 2;if(qEnvironmentVariableIsSet("SI_UI_STYLESHEET")){QFile css(qEnvironmentVariable("SI_UI_STYLESHEET"));if(css.open(QIODevice::ReadOnly))app.setStyleSheet(QString::fromUtf8(css.readAll()));}app.setStyle(QStyleFactory::create("Fusion"));MainWindow window;window.show();QCoreApplication::processEvents();
  auto* table=window.findChild<QTableWidget*>(QStringLiteral("structureList"));auto* viewer=window.findChild<MoleculeViewWidget*>(QStringLiteral("moleculeView"));auto* preview=window.findChild<DocumentPreviewWidget*>(QStringLiteral("documentPreview"));bool ok=Phase1Smoke::iconNavigation(window);
  ok&=check(table&&viewer&&preview,"three-part UI components");ok&=check(window.findChild<QToolButton*>(QStringLiteral("generateSiButton"))!=nullptr,"SI generate button");ok&=check(window.findChild<QToolButton*>(QStringLiteral("generateXyzButton"))==nullptr,"combined SI+XYZ generation replaces separate toolbar action");
  auto* mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1]))});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&enter);QDropEvent drop(QPointF(20,20),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&drop);ok&=check(waitUntil([table]{return table&&table->rowCount()==1;})&&Phase1Smoke::waitForImport(window),"Gaussian drop and parse");
  if(qEnvironmentVariableIsSet("SI_NAV_SAMPLE_DIR")){
    const auto dir=qEnvironmentVariable("SI_NAV_SAMPLE_DIR");QDir().mkpath(dir);QTest::qWait(300);window.grab().save(dir+"/structures.png");
    window.findChild<QToolButton*>("navigate_energy")->click();QTest::qWait(80);window.grab().save(dir+"/energy.png");
    window.findChild<QToolButton*>("navigate_document")->click();QTest::qWait(80);window.grab().save(dir+"/document.png");
    window.resize(1050,720);QTest::qWait(80);window.grab().save(dir+"/compact.png");window.resize(1480,900);window.findChild<QToolButton*>("navigate_structures")->click();
  }
  const si::GaussianParser parser;const auto result=parser.parse(std::filesystem::path(QString::fromLocal8Bit(argv[1]).toStdWString()));ok&=check(result.finalGeometry.size()==3,"renderer geometry");viewer->setMolecule({result.finalGeometry.front()},{result.spinDensities.front()});viewer->resize(500,350);viewer->show();viewer->setInteractionMode(MoleculeViewWidget::InteractionMode::SpinDensity);QCoreApplication::processEvents();QSignalSpy atomSpy(viewer,&MoleculeViewWidget::atomPicked);QTest::mouseClick(viewer,Qt::LeftButton,Qt::NoModifier,viewer->rect().center());ok&=check(atomSpy.count()==1,"atom picking and spin signal");viewer->setInteractionMode(MoleculeViewWidget::InteractionMode::Geometry);QSignalSpy geometrySpy(viewer,&MoleculeViewWidget::geometrySelectionChanged);QTest::mouseClick(viewer,Qt::LeftButton,Qt::NoModifier,viewer->rect().center());ok&=check(geometrySpy.count()>=1&&!viewer->selectedAtoms().isEmpty(),"geometry selection");
  si::SIDocumentModel longDocument;for(int i=0;i<120;++i)longDocument.figureCaptions.append(QStringLiteral("Long preview row %1").arg(i));window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(1);QCoreApplication::processEvents();preview->setDocumentModel(longDocument);QTest::qWait(30);auto* browser=preview->findChild<QTextBrowser*>();auto* scroll=browser?browser->verticalScrollBar():nullptr;ok&=check(scroll&&scroll->maximum()>0,"preview is scrollable");if(scroll&&scroll->maximum()>0){scroll->setValue(scroll->maximum()/2);const int before=scroll->value();longDocument.title=QStringLiteral("Updated title");preview->setDocumentModel(longDocument);QTest::qWait(30);ok&=check(scroll->value()>0&&std::abs(scroll->value()-before)<std::max(20,scroll->maximum()/10),"preview scroll position preserved after refresh");}
  window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);QCoreApplication::processEvents();ok&=Phase1Smoke::run(window);
  ok&=Phase1Smoke::reactionWorkspace(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::stageStructureThermodynamics(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::projectManagerWorkflow();
  ok&=Phase1Smoke::portableProject(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::reactionStageNames(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::bulkTitlesAndStageContext(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::reactionStageWorkflow(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::appendFiles(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::labelledFileImports(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::orderedReactionInputs(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::numberedReactionLabels(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::referenceDrafts();
  if(qEnvironmentVariableIsSet("SI_REAL_REACTION_DIR"))ok&=Phase1Smoke::realReactionInputs(qEnvironmentVariable("SI_REAL_REACTION_DIR"));
  ok&=Phase1Smoke::namedAndTsImports(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::responsiveImport(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::automaticGeometry();
  ok&=figureWorkflowSmoke();
  ok&=tableImageSmoke();
  ok&=singleLineTableSmoke();
  ok&=!unexpectedModal;
  std::cout<<(ok?"native UI smoke: PASS\n":"native UI smoke: FAIL\n");return ok?0:1;
}
