#include "DocumentPreviewWidget.hpp"
#include "si/StructureLabels.hpp"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMimeData>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>

namespace {
QString styled(const QString& value) { return si::formattedTextHtml(value); }

QString tableHtml(const si::DocumentTable& table,int index,bool includeCaption=true) {
  const auto cellText=[](QString text){
    text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\x{2028}\\x{2029}]+")),QStringLiteral(" "));
    return styled(text);
  };
  // Cell borders survive Word/Excel paste and are also supported by Qt's
  // rich-text renderer. Use one rule width for all three horizontal lines.
  QString html=QStringLiteral("<p style='margin:0;line-height:12pt'>&nbsp;</p><table style='border-collapse:collapse;font-family:Times New Roman;font-size:9pt;border:0' cellspacing='0' cellpadding='4'><thead><tr>");
  for(const auto& header:table.headers)html+=QStringLiteral("<th nowrap style='white-space:nowrap;border-top:0.75pt solid black;border-bottom:0.75pt solid black;text-align:center;font-weight:700'><b>%1</b></th>").arg(cellText(header));
  html+=QStringLiteral("</tr></thead><tbody>");
  for(int rowIndex=0;rowIndex<table.rows.size();++rowIndex){
    const auto& row=table.rows[rowIndex];
    html+=QStringLiteral("<tr>");
    for(int column=0;column<table.headers.size();++column){
      const bool bold=column==0||table.boldColumns.contains(column);
      const QString rule=rowIndex==table.rows.size()-1?QStringLiteral("border-bottom:0.75pt solid black;"):QString();
      html+=QStringLiteral("<td nowrap style='white-space:nowrap;%1text-align:%2'>%3%4%5</td>").arg(rule,"center",bold?"<b>":"",cellText(column==0?si::structureLabelMarkup(row.value(column,QStringLiteral("N/A"))):row.value(column,QStringLiteral("N/A"))),bold?"</b>":"");
    }
    html+=QStringLiteral("</tr>");
  }
  html+=QStringLiteral("</tbody></table><p style='margin:0;line-height:12pt'>&nbsp;</p>");
  if(!includeCaption)return html;
  html+=QStringLiteral("<p><b>Table S%1.</b> %2</p>").arg(table.documentNumber>0?table.documentNumber:index+1).arg(styled(table.caption));
  if(table.section==si::DocumentSection::RelativeEnergy){
    if(table.headers.contains(QStringLiteral("ΔΔE^{a}")))html+=QStringLiteral("<div><sup>a</sup> Def2-TZVPP electronic energy = sum of the two previous columns.</div>");
    if(table.headers.contains(QStringLiteral("ΔΔE_{Thermal}^{b}"))||table.headers.contains(QStringLiteral("-TΔS^{b}")))html+=QStringLiteral("<div><sup>b</sup> %1</div>").arg(table.thermochemistryNote.isEmpty()?QStringLiteral("T = 298.15 K."):table.thermochemistryNote.toHtmlEscaped());
    if(const auto note=si::relativeGibbsFootnote(table);!note.isEmpty())html+=QStringLiteral("<div><sup>c</sup> %1</div>").arg(styled(note));
  }
  return html;
}
QString htmlDocument(const QString& body){
  return QStringLiteral("<html><head><meta charset='utf-8'><style>body{font-family:'Times New Roman';font-size:9pt;color:#111;background:white}h1{font-size:12pt}p{margin:4px 0 12px}</style></head><body>%1</body></html>").arg(body);
}
void keepTableCellsOnOneLine(QTextDocument* document){
  // Applying each block/cell format separately repeatedly lays out the entire
  // document. Batch the edits so a large coordinate section remains responsive.
  QTextCursor edit(document);edit.beginEditBlock();
  // Qt does not reliably apply HTML nowrap to table cells. Set the native block
  // property so the table's minimum width includes the entire unbroken cell.
  for(auto block=document->begin();block.isValid();block=block.next()){
    QTextCursor cursor(block);
    if(!cursor.currentTable())continue;
    auto format=block.blockFormat();format.setNonBreakableLines(true);
    cursor.setBlockFormat(format);
  }
  // Qt ignores per-side CSS cell borders on HTML import. Apply the same
  // rules natively for the preview and PNG rather than mixing painted rules
  // with HTML rules (which produces inconsistent line widths).
  for(auto* frame:document->rootFrame()->childFrames()){
    auto* table=qobject_cast<QTextTable*>(frame);if(!table)continue;
    auto tableFormat=table->format();tableFormat.setBorder(0);tableFormat.setBorderCollapse(true);table->setFormat(tableFormat);
    for(int row=0;row<table->rows();++row)for(int column=0;column<table->columns();++column){
      auto cell=table->cellAt(row,column);auto format=cell.format().toTableCellFormat();
      format.setTopBorder(row==0?1.0:0.0);
      format.setBottomBorder(row==0||row==table->rows()-1?1.0:0.0);
      format.setLeftBorder(0);format.setRightBorder(0);
      format.setTopBorderStyle(QTextFrameFormat::BorderStyle_Solid);format.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
      format.setTopBorderBrush(Qt::black);format.setBottomBorderBrush(Qt::black);cell.setFormat(format);
    }
  }
  edit.endEditBlock();
}
}

DocumentPreviewWidget::DocumentPreviewWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("documentPreview"));
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
  auto* controls=new QHBoxLayout;
  tableSelector_=new QComboBox(this);tableSelector_->setObjectName("previewTableSelector");
  tableSelector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);tableSelector_->setMinimumContentsLength(24);
  tableSelector_->addItem(tr("전체 문서 (Full Document)"),QStringLiteral("all"));
  copyButton_=new QPushButton(tr("선택 표 복사 (Copy Table)"),this);copyButton_->setObjectName("copyPreviewTableButton");copyButton_->setEnabled(false);
  copyButton_->setToolTip(tr("표를 선택한 후 복사하여 Word 또는 Excel에 붙여넣으세요."));
  copyImageButton_=new QPushButton(tr("표 이미지 복사 (Copy Image)"),this);copyImageButton_->setObjectName("copyPreviewTableImageButton");copyImageButton_->setEnabled(false);
  copyImageButton_->setToolTip(tr("선택한 표만 투명 배경 PNG 이미지로 복사합니다. 캡션과 각주는 제외됩니다."));
  controls->addWidget(tableSelector_,1);controls->addWidget(copyButton_);controls->addWidget(copyImageButton_);layout->addLayout(controls);
  preview_=new QTextBrowser(this);preview_->setOpenExternalLinks(false);
  preview_->setStyleSheet(QStringLiteral("QTextBrowser{background:white;border:0;padding:12px;}"));layout->addWidget(preview_);
  connect(tableSelector_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{lastHtml_.clear();renderPreview();preview_->verticalScrollBar()->setValue(0);});
  connect(copyButton_,&QPushButton::clicked,this,&DocumentPreviewWidget::copySelectedTable);
  connect(copyImageButton_,&QPushButton::clicked,this,&DocumentPreviewWidget::copySelectedTableImage);
}

void DocumentPreviewWidget::setDocumentModel(const si::SIDocumentModel& model) {
  const auto selected=tableSelector_->currentData().toString();
  model_=model;
  {
    QSignalBlocker blocker(tableSelector_);tableSelector_->clear();
    tableSelector_->addItem(tr("전체 문서 (Full Document)"),QStringLiteral("all"));
    QHash<int,int> counts;
    for(int i=0;i<model_.tables.size();++i){
      const auto& table=model_.tables[i];const int section=static_cast<int>(table.section);
      const auto key=QStringLiteral("%1:%2").arg(section).arg(counts[section]++);
      tableSelector_->addItem(QStringLiteral("Table S%1 — %2").arg(table.documentNumber>0?table.documentNumber:i+1).arg(table.caption),key);
      tableSelector_->setItemData(i+1,i,Qt::UserRole+1);
    }
    tableSelector_->setCurrentIndex(std::max(0,tableSelector_->findData(selected)));
  }
  renderPreview();
}

void DocumentPreviewWidget::renderPreview() {
  const int selected=tableSelector_->currentIndex()>0?tableSelector_->currentData(Qt::UserRole+1).toInt():-1;
  copyButton_->setEnabled(selected>=0&&selected<model_.tables.size());
  copyImageButton_->setEnabled(copyButton_->isEnabled());
  QString body;
  if(selected>=0&&selected<model_.tables.size())body=tableHtml(model_.tables[selected],selected);
  else {
    const auto& model=model_;
    if(model.stageOrder.isEmpty()||!model.computationalDetails.isEmpty()||!model.referencesText.isEmpty())body=QStringLiteral("<h1>%1</h1>").arg(styled(QStringLiteral("Computational Details")));
    const auto details=si::computationalDetailsBody(model.computationalDetails);
    if(!details.isEmpty())body+=QStringLiteral("<p>%1</p>").arg(styled(details));
    if(!model.referencesText.trimmed().isEmpty())body+=QStringLiteral("<h2>References</h2><p>%1</p>").arg(styled(model.referencesText).replace('\n',"<br>"));
    const int figureWidth=std::max(200,std::min(600,preview_->viewport()->width()-30));auto figureHtml=[&](int i){QString html;if(i<model.figures.size())html+=QStringLiteral("<p><img width='%3' height='%1' src='data:image/png;base64,%2'></p>").arg(figureWidth*model.figures[i].pixelHeight/std::max(1,model.figures[i].pixelWidth)).arg(QString::fromLatin1(model.figures[i].png.toBase64())).arg(figureWidth);const auto caption=model.figureCaptions.value(i,i<model.figures.size()?model.figures[i].structureNames.join(" / "):QString());const int number=i<model.figures.size()&&model.figures[i].documentNumber>0?model.figures[i].documentNumber:i+1;return html+QStringLiteral("<p><b>Figure S%1.</b> %2</p>").arg(number).arg(styled(caption));};
    if(model.stageOrder.isEmpty()){for(int i=0;i<std::max(model.figures.size(),model.figureCaptions.size());++i)body+=figureHtml(i);for(int i=0;i<model.tables.size();++i)body+=tableHtml(model.tables[i],i);}
    else {for(int i=0;i<model.figures.size();++i)if(model.figures[i].stageId.isEmpty())body+=figureHtml(i);for(const auto& id:model.stageOrder){body+=QStringLiteral("<h1>%1</h1>").arg(styled(model.stageNames.value(id)));for(int i=0;i<model.figures.size();++i)if(model.figures[i].stageId==id)body+=figureHtml(i);for(int i=0;i<model.tables.size();++i)if(model.tables[i].stageId==id)body+=tableHtml(model.tables[i],i);}for(int i=0;i<model.tables.size();++i)if(model.tables[i].stageId.isEmpty())body+=tableHtml(model.tables[i],i);}    if(!model.coordinates.isEmpty()){
      const int digits=model.precisionOverride>0?model.precisionOverride:6;
      body+=QStringLiteral("<h1>Cartesian Coordinates</h1>");
      for(const auto& block:model.coordinates){
        body+=QStringLiteral("<p><b>%1</b><br>").arg(styled(si::structureLabelMarkup(block.structureName)));
        for(const auto& atom:block.atoms)body+=QStringLiteral("%1&nbsp;&nbsp;%2&nbsp;&nbsp;%3&nbsp;&nbsp;%4<br>").arg(QString::fromStdString(atom.symbol),QString::number(atom.xyz[0],'f',digits),QString::number(atom.xyz[1],'f',digits),QString::number(atom.xyz[2],'f',digits));
        body+=QStringLiteral("</p>");
      }
    }
    if(model.empty())body+=tr("<p>입력을 추가하면 SI 문서 미리보기가 표시됩니다.</p>");
  }
  const auto html=htmlDocument(body);if(html==lastHtml_)return;
  auto* scroll=preview_->verticalScrollBar();const int oldValue=scroll->value(),oldMaximum=scroll->maximum();
  const bool wasAtBottom=oldMaximum>0&&oldValue>=oldMaximum-2;
  const double relative=oldMaximum>0?static_cast<double>(oldValue)/oldMaximum:0.0;
  lastHtml_=html;preview_->setHtml(html);keepTableCellsOnOneLine(preview_->document());
  QTimer::singleShot(0,preview_,[scroll,oldValue,oldMaximum,wasAtBottom,relative]{const int maximum=scroll->maximum();if(wasAtBottom)scroll->setValue(maximum);else if(oldMaximum>0)scroll->setValue(qRound(relative*maximum));else scroll->setValue(std::min(oldValue,maximum));});
}

void DocumentPreviewWidget::copySelectedTable() {
  if(tableSelector_->currentIndex()<=0)return;
  const int index=tableSelector_->currentData(Qt::UserRole+1).toInt();
  if(index<0||index>=model_.tables.size())return;
  const auto& table=model_.tables[index];
  QStringList textRows{table.headers.join('\t')};
  for(const auto& row:table.rows){QStringList cells;for(int col=0;col<table.headers.size();++col)cells.append(row.value(col,QStringLiteral("N/A")));textRows.append(cells.join('\t'));}
  auto* mime=new QMimeData;mime->setHtml(htmlDocument(tableHtml(table,index)));mime->setText(textRows.join('\n'));
  QApplication::clipboard()->setMimeData(mime);
}

void DocumentPreviewWidget::copySelectedTableImage() {
  if(tableSelector_->currentIndex()<=0)return;
  const int index=tableSelector_->currentData(Qt::UserRole+1).toInt();
  if(index<0||index>=model_.tables.size())return;

  // Render the complete table independently of the visible viewport or scroll position.
  auto html=tableHtml(model_.tables[index],index,false);
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setHtml(QStringLiteral("<html><body style='color:black'>%1</body></html>").arg(html));
  keepTableCellsOnOneLine(&document);
  document.adjustSize();
  document.setTextWidth(document.idealWidth());
  constexpr int scale=3;
  const QSize size(qCeil(document.size().width()*scale),qCeil(document.size().height()*scale));
  QImage image(size,QImage::Format_ARGB32_Premultiplied);
  if(image.isNull())return;
  image.fill(Qt::transparent);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(scale,scale);
    document.drawContents(&painter);
  }
  // Trim document margins down to the actual ink without losing antialiased edges.
  int left=image.width(),top=image.height(),right=-1,bottom=-1;
  for(int y=0;y<image.height();++y){
    const auto* pixels=reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for(int x=0;x<image.width();++x)if(qAlpha(pixels[x])){
      left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
    }
  }
  if(right<left||bottom<top)return;
  image=image.copy(QRect(QPoint(left,top),QPoint(right,bottom)));
  image.setDotsPerMeterX(qRound(96.0*scale/0.0254));
  image.setDotsPerMeterY(qRound(96.0*scale/0.0254));
  QByteArray png;
  QBuffer buffer(&png);
  if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))return;
  auto* mime=new QMimeData;
  mime->setImageData(image);
  // Explicit PNG preserves alpha for Windows applications that prefer this format.
  mime->setData(QStringLiteral("image/png"),png);
  QApplication::clipboard()->setMimeData(mime);
}
