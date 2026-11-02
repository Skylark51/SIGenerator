#include "DocumentPreviewWidget.hpp"
#include "MainWindow.hpp"
#include "MoleculeViewWidget.hpp"
#include "si/GaussianParser.hpp"
#include <QApplication>
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
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QTemporaryDir>
#include <QFile>
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
  model.tables.append(table);preview.setDocumentModel(model);
  auto* selector=preview.findChild<QComboBox*>("previewTableSelector");
  auto* imageButton=preview.findChild<QPushButton*>("copyPreviewTableImageButton");
  auto* browser=preview.findChild<QTextBrowser*>();
  if(!selector||!imageButton||!browser)return check(false,"single-line table controls");
  selector->setCurrentIndex(1);QCoreApplication::processEvents();
  bool ok=true;
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
  ok&=check(html.contains("white-space:nowrap")&&!html.contains("<br>"),"HTML copy prevents wrapping and flattens line breaks within cells");
  return ok;
}
}

struct Phase1Smoke {
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
        " 1 6 0 0 0 0\n 2 6 0 1.4 0 0\n 3 8 0 0 1.3 0\n 4 1 0 0 0 %1\n -----\n Normal termination of Gaussian 16\n").arg(index?1.7:1.0).toUtf8());
      file.close();paths.append(path);
    }
    window.parsePaths(paths);if(!waitUntil([&]{return !window.parsing_;}))return false;
    window.refreshDocumentModel();
    auto* table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    ok&=check(table&&table->rows.size()==2&&table->headers.contains("C(1)-H(4) (Å)")&&table->caption.contains("RMSD"),"empty geometry input produces automatic reaction distance table");
    ok&=check(window.geometryIndices_->toPlainText().isEmpty(),"automatic suggestions preserve empty input for recalculation");
    window.geometryIndices_->setPlainText("1 2 3");window.refreshDocumentModel();
    table=window.documentModel_.table(si::DocumentSection::SelectedGeometry);
    ok&=check(table&&table->headers.size()==2&&table->headers[1].contains("°")&&!table->caption.contains("RMSD"),"manual geometry overrides automatic suggestions");
    window.geometryIndices_->clear();window.parsePaths({paths.front()});if(!waitUntil([&]{return !window.parsing_;}))return false;
    window.refreshDocumentModel();
    ok&=check(!window.documentModel_.table(si::DocumentSection::SelectedGeometry)&&!window.geometryAutoInfo_->text().isEmpty(),"single structure shows explanation instead of automatic table");
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
    window.fileNames_->paste();window.addNamedFiles();
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
    QApplication::clipboard()->setText(QString("5B-TS\t%1\r\n5C\t%2").arg(tsName,ircName));window.fileNames_->paste();window.addNamedFiles();
    ok&=check(waitForImport(window)&&window.sourcePaths()==QStringList({folder.filePath(tsName),folder.filePath(ircName)})&&window.structureLabels()==QStringList({"5B-TS","5C"})&&!window.fileNamesInfo_->text().contains("파일 없음"),"screenshot-shaped Excel paste preserves TS/IRC labels across automatic endpoint discovery");
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
    window.siOutputPath_->setText(folder.filePath("empty-details.docx"));
    QTimer::singleShot(0,&window,[&window]{if(auto* dialog=window.findChild<FigureSetupDialog*>())dialog->findChild<QPushButton*>("nextFigureGroup")->click();});
    window.exportSiDocument();
    ok&=check(waitUntil([&]{return QFile::exists(window.siOutputPath_->text())&&window.status_->text().contains("생성 완료");}),"SI export succeeds with both computational details and references empty");
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
    window.fileNames_->paste();
    auto* add=window.findChild<QPushButton*>("addNamedFiles");if(!add)return false;add->click();
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
      absolute->rows.front()==QStringList({"1A-freq","-100.00000000","-101.00000000","18.826","25.100","50.000","-0.01000000"}),"legacy absolute columns and Gaussian printed kcal values");
    const auto* relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
    const QStringList expectedRelative{"3A-freq","-62.75","-62.75","-125.50","+0.30","+0.20","-1.49","+1.26","+0.00","-125.24"};
    ok&=check(relative&&relative->headers.size()==10&&relative->rows[1]==expectedRelative&&relative->boldColumns==QVector<int>({3,9}),"restored nine-value relative table with Python Excel formulas and signs");
    for(bool dispersion:{false,true})for(bool complexation:{false,true}){
      window.includeDispersion_->setChecked(dispersion);window.includeComplexation_->setChecked(complexation);window.refreshDocumentModel();
      relative=window.documentModel_.table(si::DocumentSection::RelativeEnergy);
      ok&=check(relative&&relative->headers.size()==8+int(dispersion)+int(complexation)&&relative->headers.contains("Δ_{Dispersion}")==dispersion&&relative->headers.contains("Δ_{Complexation}")==complexation,"automatic legacy optional columns");
      const double expected=-0.2*si::HartreeToKcal+0.5-1.49075+(dispersion?0.002*si::HartreeToKcal:0.0);
      ok&=check(relative&&relative->rows[1].back()==si::formatNumber(expected,2,0),"relative Gibbs includes only enabled correction terms");
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
    window.siOutputPath_->setText(folder.filePath("SI.docx"));
    QTimer::singleShot(0,&window,[&window]{if(auto* dialog=window.findChild<FigureSetupDialog*>())dialog->findChild<QPushButton*>("nextFigureGroup")->click();});
    window.exportSiDocument();
    ok&=check(waitUntil([&]{return window.status_->text().contains("생성 완료")&&window.status_->text().contains("SI.docx");}),"SI export UI flow");
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
  QApplication app(argc,argv);bool unexpectedModal=false;QTimer modalGuard;modalGuard.setInterval(4000);QObject::connect(&modalGuard,&QTimer::timeout,&app,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){std::cerr<<"FAILED: unexpected modal: "<<box->text().toStdString()<<'\n';unexpectedModal=true;box->reject();}});modalGuard.start();{QSettings test("SI Generator","SIGeneratorTests");test.clear();}if(argc!=2)return 2;MainWindow window;window.show();QCoreApplication::processEvents();
  auto* table=window.findChild<QTableWidget*>(QStringLiteral("structureList"));auto* viewer=window.findChild<MoleculeViewWidget*>(QStringLiteral("moleculeView"));auto* preview=window.findChild<DocumentPreviewWidget*>(QStringLiteral("documentPreview"));bool ok=true;
  ok&=check(table&&viewer&&preview,"three-part UI components");ok&=check(window.findChild<QToolButton*>(QStringLiteral("generateSiButton"))!=nullptr,"SI generate button");ok&=check(window.findChild<QToolButton*>(QStringLiteral("generateXyzButton"))!=nullptr,"XYZ generate button");
  auto* mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1]))});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&enter);QDropEvent drop(QPointF(20,20),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&window,&drop);ok&=check(waitUntil([table]{return table&&table->rowCount()==1;})&&Phase1Smoke::waitForImport(window),"Gaussian drop and parse");
  const si::GaussianParser parser;const auto result=parser.parse(std::filesystem::path(QString::fromLocal8Bit(argv[1]).toStdWString()));ok&=check(result.finalGeometry.size()==3,"renderer geometry");viewer->setMolecule({result.finalGeometry.front()},{result.spinDensities.front()});viewer->resize(500,350);viewer->show();viewer->setInteractionMode(MoleculeViewWidget::InteractionMode::SpinDensity);QCoreApplication::processEvents();QSignalSpy atomSpy(viewer,&MoleculeViewWidget::atomPicked);QTest::mouseClick(viewer,Qt::LeftButton,Qt::NoModifier,viewer->rect().center());ok&=check(atomSpy.count()==1,"atom picking and spin signal");viewer->setInteractionMode(MoleculeViewWidget::InteractionMode::Geometry);QSignalSpy geometrySpy(viewer,&MoleculeViewWidget::geometrySelectionChanged);QTest::mouseClick(viewer,Qt::LeftButton,Qt::NoModifier,viewer->rect().center());ok&=check(geometrySpy.count()>=1&&!viewer->selectedAtoms().isEmpty(),"geometry selection");
  si::SIDocumentModel longDocument;for(int i=0;i<120;++i)longDocument.figureCaptions.append(QStringLiteral("Long preview row %1").arg(i));preview->setDocumentModel(longDocument);QTest::qWait(30);auto* browser=preview->findChild<QTextBrowser*>();auto* scroll=browser?browser->verticalScrollBar():nullptr;ok&=check(scroll&&scroll->maximum()>0,"preview is scrollable");if(scroll&&scroll->maximum()>0){scroll->setValue(scroll->maximum()/2);const int before=scroll->value();longDocument.title=QStringLiteral("Updated title");preview->setDocumentModel(longDocument);QTest::qWait(30);ok&=check(scroll->value()>0&&std::abs(scroll->value()-before)<std::max(20,scroll->maximum()/10),"preview scroll position preserved after refresh");}
  ok&=Phase1Smoke::run(window);
  ok&=Phase1Smoke::appendFiles(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::labelledFileImports(QString::fromLocal8Bit(argv[1]));
  ok&=Phase1Smoke::orderedReactionInputs(QString::fromLocal8Bit(argv[1]));
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
