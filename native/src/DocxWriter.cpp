#include "DocxWriter.hpp"
#include "si/StructureLabels.hpp"

#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <algorithm>
#include <cstdint>

namespace {
quint32 crc32(const QByteArray& data) {
  quint32 crc=0xffffffffU;
  for(const auto ch:data){crc^=static_cast<unsigned char>(ch);for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1U)?0xedb88320U:0U);}
  return ~crc;
}
void u16(QIODevice& file,quint16 value){file.putChar(static_cast<char>(value&255));file.putChar(static_cast<char>(value>>8));}
void u32(QIODevice& file,quint32 value){u16(file,static_cast<quint16>(value&0xffff));u16(file,static_cast<quint16>(value>>16));}
struct Entry{QByteArray name;quint32 crc{},size{},offset{};};
QString xml(QString value){return value.replace('&',"&amp;").replace('<',"&lt;").replace('>',"&gt;").replace('"',"&quot;");}

QString run(const QString& text,bool bold=false,int halfPoints=20,const QString& vertical={}) {
  QString properties=QStringLiteral("<w:rFonts w:ascii=\"Times New Roman\" w:hAnsi=\"Times New Roman\" w:eastAsia=\"Times New Roman\" w:cs=\"Times New Roman\"/><w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(halfPoints);
  properties+=QStringLiteral("<w:color w:val=\"000000\"/>");
  if(bold)properties+=QStringLiteral("<w:b/><w:bCs/>");
  if(!vertical.isEmpty())properties+=QStringLiteral("<w:vertAlign w:val=\"%1\"/>").arg(vertical);
  QString content=xml(text);content.replace(QStringLiteral("\r"),QString());content.replace(QStringLiteral("\n"),QStringLiteral("</w:t><w:br/><w:t xml:space=\"preserve\">"));
  return QStringLiteral("<w:r><w:rPr>%1</w:rPr><w:t xml:space=\"preserve\">%2</w:t></w:r>").arg(properties,content);
}

QString styledRuns(const QString& text,bool bold=false,int halfPoints=20) {
  QString output; static const QRegularExpression re(QStringLiteral("([_^])(?:\\{([^{}]+)\\}|([IVX]+|[-+]?\\d+|[^_^\\s]))")); int position=0;
  auto matches=re.globalMatch(text);
  while(matches.hasNext()){const auto match=matches.next();if(match.capturedStart()>position)output+=run(text.mid(position,match.capturedStart()-position),bold,halfPoints);const auto token=match.captured(2).isEmpty()?match.captured(3):match.captured(2);output+=run(token,bold,halfPoints,match.captured(1)==QStringLiteral("_")?QStringLiteral("subscript"):QStringLiteral("superscript"));position=match.capturedEnd();}
  if(position<text.size())output+=run(text.mid(position),bold,halfPoints);
  return output.isEmpty()?run(QString(),bold,halfPoints):output;
}

QString paragraph(const QString& text,bool bold=false,int halfPoints=20,const QString& align=QStringLiteral("left"),int before=0,int after=0,const QString& style=QStringLiteral("Normal"),const QString& properties={}) {
  return QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"%5\"/>%6<w:jc w:val=\"%1\"/><w:spacing w:before=\"%2\" w:after=\"%3\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>%4</w:p>").arg(align).arg(before).arg(after).arg(styledRuns(text,bold,halfPoints),style,properties);
}

QString field(const QString& instruction,const QString& placeholder,bool bold=false) {
  const QString properties=bold?QStringLiteral("<w:rPr><w:b/><w:bCs/></w:rPr>"):QString();
  return QStringLiteral("<w:r>%3<w:fldChar w:fldCharType=\"begin\" w:dirty=\"true\"/></w:r><w:r>%3<w:instrText xml:space=\"preserve\"> %1 </w:instrText></w:r><w:r>%3<w:fldChar w:fldCharType=\"separate\"/></w:r>%2<w:r>%3<w:fldChar w:fldCharType=\"end\"/></w:r>").arg(xml(instruction),run(placeholder,bold),properties);
}

QString sequenceCaption(const QString& label,const QString& sequence,int number,const QString& title,bool keepNext=false) {
  // CHARFORMAT takes its format from the first instruction character.
  // Bold both the SEQ instruction and its cached result so Word field
  // updates keep every digit of Table S10, S11, ... bold as well.
  return QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"Caption\"/>%5<w:keepLines/><w:spacing w:before=\"45\" w:after=\"125\"/><w:jc w:val=\"left\"/><w:outlineLvl w:val=\"1\"/></w:pPr>%1%2%3%4</w:p>")
    .arg(run(label,true),field(QStringLiteral("SEQ %1 \\* ARABIC \\* CHARFORMAT").arg(sequence),QString::number(number),true),run(QStringLiteral(". "),true),styledRuns(title),keepNext?QStringLiteral("<w:keepNext/>"):QString());
}

QString cell(const QString& value,bool bold,bool header=false,bool first=false,int halfPoints=18,int widthTwips=0,bool keepNext=false) {
  QString text=value;text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\x{2028}\\x{2029}]+")),QStringLiteral(" "));
  if(first&&!header)text=si::structureLabelMarkup(text);
  const QString keep=keepNext?QStringLiteral("<w:keepNext/>"):QString();
  const QString rule=header?QStringLiteral("<w:tcBorders><w:bottom w:val=\"single\" w:sz=\"6\" w:color=\"000000\"/></w:tcBorders>"):QString();
  return QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%3\" w:type=\"dxa\"/>%1<w:vAlign w:val=\"center\"/></w:tcPr><w:p><w:pPr>%4<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"210\" w:lineRule=\"auto\"/><w:jc w:val=\"center\"/></w:pPr>%2</w:p></w:tc>")
    .arg(rule,styledRuns(text,bold,halfPoints)).arg(widthTwips).arg(keep);
}

QString tableXml(const si::DocumentTable& source) {
  if(source.headers.isEmpty())return {};
  const int columnCount=static_cast<int>(source.headers.size());
  const int halfPoints=source.section==si::DocumentSection::RelativeEnergy&&columnCount>8?16:18;
  // US Letter, 1-inch side margins: 9360 twips available for the body.
  constexpr int usableWidth=9360;
  const int labelWidth=columnCount==1?usableWidth:(columnCount>=9?2200:columnCount>=7?2350:2700);
  QVector<int> widths(columnCount);
  if(columnCount==1)widths[0]=usableWidth;
  else {
    widths[0]=labelWidth;
    const int numericWidth=(usableWidth-labelWidth)/(columnCount-1);
    for(int column=1;column<columnCount;++column)widths[column]=numericWidth;
    widths.back()+=usableWidth-labelWidth-numericWidth*(columnCount-1);
  }
  QString table=QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"9360\" w:type=\"dxa\"/><w:jc w:val=\"center\"/><w:tblBorders><w:top w:val=\"single\" w:sz=\"6\" w:color=\"000000\"/><w:left w:val=\"nil\"/><w:bottom w:val=\"single\" w:sz=\"6\" w:color=\"000000\"/><w:right w:val=\"nil\"/><w:insideH w:val=\"nil\"/><w:insideV w:val=\"nil\"/></w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:top w:w=\"18\" w:type=\"dxa\"/><w:left w:w=\"40\" w:type=\"dxa\"/><w:bottom w:w=\"18\" w:type=\"dxa\"/><w:right w:w=\"40\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>");
  for(int column=0;column<columnCount;++column)table+=QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(widths[column]);
  table+=QStringLiteral("</w:tblGrid><w:tr><w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>");
  for(int column=0;column<columnCount;++column)table+=cell(source.headers[column],true,true,false,halfPoints,widths[column],true);
  table+=QStringLiteral("</w:tr>");
  for(qsizetype i=0;i<source.rows.size();++i){
    const auto& row=source.rows[i];
    table+=QStringLiteral("<w:tr><w:trPr><w:cantSplit/></w:trPr>");
    for(int column=0;column<columnCount;++column)table+=cell(column<row.size()?row[column]:QStringLiteral("N/A"),column==0||source.boldColumns.contains(column),false,column==0,halfPoints,widths[column],i==source.rows.size()-1);
    table+=QStringLiteral("</w:tr>");
  }
  return table+QStringLiteral("</w:tbl>");
}

int coordinateFieldWidth(const si::SIDocumentModel& model){
  const int digits=model.precisionOverride>0?model.precisionOverride:6;
  int longest=0;
  for(const auto& block:model.coordinates)for(const auto& atom:block.atoms)
    for(double coordinate:atom.xyz)longest=std::max(longest,int(QString::number(coordinate,'f',digits).size()));
  return std::max(620,60*longest+100);
}

QString coordinateBody(const si::SIDocumentModel& model,int numericWidth) {
  constexpr int halfPoints=12;
  QString body;
  const int digits=model.precisionOverride>0?model.precisionOverride:6;
  const auto tabs=QStringLiteral("<w:tabs><w:tab w:val=\"right\" w:pos=\"%1\"/><w:tab w:val=\"right\" w:pos=\"%2\"/><w:tab w:val=\"right\" w:pos=\"%3\"/></w:tabs>")
    .arg(180+numericWidth).arg(180+2*numericWidth).arg(180+3*numericWidth);
  for(const auto& block:model.coordinates){
    body+=QStringLiteral("<w:p><w:pPr><w:keepNext/><w:spacing w:before=\"65\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>%1</w:p>").arg(styledRuns(si::structureLabelMarkup(block.structureName),true,halfPoints));
    for(const auto& atom:block.atoms){
      QString atoms=run(QString::fromStdString(atom.symbol),false,halfPoints);
      for(double coordinate:atom.xyz){
        atoms+=QStringLiteral("<w:r><w:tab/></w:r>");
        atoms+=run(QString::number(coordinate,'f',digits),false,halfPoints);
      }
      body+=QStringLiteral("<w:p><w:pPr>%1<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"220\" w:lineRule=\"auto\"/></w:pPr>%2</w:p>").arg(tabs,atoms);
    }
  }
  return body;
}

QString envelope(const QString& body,const QString& section) {
  return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>%1%2</w:body></w:document>").arg(body,section);
}
QString normalSection(){return QStringLiteral("<w:sectPr><w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>");}

bool writePackage(const QString& path,const QString& document,QString* error,const QVector<si::DocumentFigure>& figures={}) {
  QString contentTypes=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/><Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/><Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/></Types>");
  const QString rootRels=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
  QString docRels=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/><Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/><Relationship Id=\"rIdFooter\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/></Relationships>");
  const QString styles=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Times New Roman\" w:hAnsi=\"Times New Roman\"/><w:color w:val=\"000000\"/><w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/></w:rPr></w:rPrDefault></w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style><w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"24\"/></w:rPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/><w:rPr><w:b/><w:sz w:val=\"28\"/></w:rPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:b/></w:rPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"Caption\"><w:name w:val=\"Caption\"/><w:basedOn w:val=\"Normal\"/></w:style></w:styles>");
  const QString settings=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:updateFields w:val=\"true\"/></w:settings>");
  const QString footer=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:ftr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:p><w:pPr><w:pStyle w:val=\"Normal\"/><w:jc w:val=\"right\"/></w:pPr>%1%2</w:p></w:ftr>").arg(run("S"),field("PAGE","1"));
  QString imageRels;
  for(int i=0;i<figures.size();++i)imageRels+=QStringLiteral("<Relationship Id=\"rIdFigure%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/figure%1.png\"/>").arg(i+1);
  docRels.replace("</Relationships>",imageRels+"</Relationships>");
  if(!figures.isEmpty())contentTypes.replace("</Types>","<Default Extension=\"png\" ContentType=\"image/png\"/></Types>");
  const QVector<QPair<QString,QString>> xmlFiles={{QStringLiteral("[Content_Types].xml"),contentTypes},{QStringLiteral("_rels/.rels"),rootRels},{QStringLiteral("word/_rels/document.xml.rels"),docRels},{QStringLiteral("word/styles.xml"),styles},{QStringLiteral("word/settings.xml"),settings},{QStringLiteral("word/document.xml"),document},{QStringLiteral("word/footer1.xml"),footer}};
  QVector<QPair<QString,QByteArray>> files;
  for(const auto& pair:xmlFiles)files.append({pair.first,pair.second.toUtf8()});
  for(int i=0;i<figures.size();++i)files.append({QString("word/media/figure%1.png").arg(i+1),figures[i].png});
  QSaveFile out(path);if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=out.errorString();return false;}QVector<Entry> entries;
  for(const auto& pair:files){const QByteArray name=pair.first.toUtf8(),data=pair.second;Entry item{name,crc32(data),static_cast<quint32>(data.size()),static_cast<quint32>(out.pos())};u32(out,0x04034b50);u16(out,20);u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,item.crc);u32(out,item.size);u32(out,item.size);u16(out,static_cast<quint16>(name.size()));u16(out,0);out.write(name);out.write(data);entries.append(item);}
  const quint32 central=static_cast<quint32>(out.pos());for(const auto& item:entries){u32(out,0x02014b50);u16(out,20);u16(out,20);u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,item.crc);u32(out,item.size);u32(out,item.size);u16(out,static_cast<quint16>(item.name.size()));u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,0);u32(out,item.offset);out.write(item.name);}const quint32 centralSize=static_cast<quint32>(out.pos())-central;u32(out,0x06054b50);u16(out,0);u16(out,0);u16(out,static_cast<quint16>(entries.size()));u16(out,static_cast<quint16>(entries.size()));u32(out,centralSize);u32(out,central);u16(out,0);if(!out.commit()){if(error)*error=out.errorString();return false;}return true;
}
}

bool DocxWriter::writeDocument(const QString& path,const si::SIDocumentModel& model,QString* error) {
  auto heading=[](const QString& text,bool newPage=false){
    return paragraph(text,true,24,"left",120,60,"Heading1",
      newPage?QStringLiteral("<w:keepNext/><w:pageBreakBefore/>"):QStringLiteral("<w:keepNext/>"));
  };
  const QString pageBreak=QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
  QString body=heading("Computational Details");
  for(const auto& text:{model.manuscriptTitle,model.authors,model.affiliations,model.correspondingEmail})
    if(!text.trimmed().isEmpty())body+=paragraph(text);
  const auto details=si::computationalDetailsBody(model.computationalDetails);
  if(!details.isEmpty())body+=paragraph(details);
  if(!model.referencesText.trimmed().isEmpty())body+=heading("References")+paragraph(model.referencesText);
  const bool hasFrontMatter=!details.isEmpty()||!model.referencesText.trimmed().isEmpty()||
    !model.manuscriptTitle.trimmed().isEmpty()||!model.authors.trimmed().isEmpty()||
    !model.affiliations.trimmed().isEmpty()||!model.correspondingEmail.trimmed().isEmpty();
  for(const auto& figure:model.figures)if(figure.png.isEmpty()||figure.pixelWidth<=0||figure.pixelHeight<=0){if(error)*error="Invalid molecular figure image.";return false;}
  int figureNumber=0;
  const int figureCount=std::max(model.figures.size(),model.figureCaptions.size());
  auto writeFigure=[&](int i){
    if(i<model.figures.size()){
      const auto& figure=model.figures[i];

      const qint64 preferredWidth=5943600*std::clamp(figure.pageWidthFraction,0.1,1.0);
      const qint64 cx=std::min(preferredWidth,qint64(6858000)*figure.pixelWidth/figure.pixelHeight);
      const qint64 cy=cx*figure.pixelHeight/figure.pixelWidth;
      body+=QStringLiteral("<w:p><w:pPr><w:keepNext/><w:jc w:val=\"center\"/></w:pPr><w:r><w:drawing>"
        "<wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:docPr id=\"%3\" name=\"Figure S%3\" descr=\"%4\"/>"
        "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"figure%3.png\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"rIdFigure%3\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>")
        .arg(cx).arg(cy).arg(i+1).arg(xml(figure.structureNames.join(" / ")));
    }
    const auto caption=model.figureCaptions.value(i,i<model.figures.size()?model.figures[i].structureNames.join(" / "):QString());
    body+=sequenceCaption("Figure S","Figure_S",++figureNumber,caption);
  }
  ;int tableNumber=0;
  auto writeTable=[&](const si::DocumentTable& table){if(table.section==si::DocumentSection::Validation||table.headers.isEmpty())return;
    const auto section=table.section;
      body+=tableXml(table)+sequenceCaption("Table S","Table_S",++tableNumber,table.caption,section==si::DocumentSection::RelativeEnergy);
      if(section==si::DocumentSection::RelativeEnergy){
        auto footnote=[&](const QString& text){
          return paragraph(text,false,16,"left",0,0,"Normal","<w:keepNext/><w:keepLines/>");
        };
        if(table.headers.contains(QStringLiteral("ΔΔE^{a}")))
          body+=footnote("^{a} Def2-TZVPP electronic energy = sum of the two previous columns.");
        if(table.headers.contains(QStringLiteral("ΔΔE_{Thermal}^{b}"))||table.headers.contains(QStringLiteral("-TΔS^{b}")))
          body+=footnote(QStringLiteral("^{b} ")+(table.thermochemistryNote.isEmpty()?QStringLiteral("T = 298.15 K."):table.thermochemistryNote));
        if(const auto note=si::relativeGibbsFootnote(table);!note.isEmpty())body+=footnote(QStringLiteral("^{c} ")+note);
        QString basis=QStringLiteral("current/default behavior");
        const QString basisMarker=QStringLiteral("Energy basis: ");
        const int basisPosition=table.caption.indexOf(basisMarker);
        if(basisPosition>=0){
          basis=table.caption.mid(basisPosition+basisMarker.size()).trimmed();
          if(basis.endsWith('.'))basis.chop(1);
        }
        body+=paragraph(QStringLiteral("Relative energies were calculated with respect to the selected reference. Energy basis: %1. Conversion factor: 1 Hartree = %2 kcal mol^{-1}.")
          .arg(basis,QString::number(si::HartreeToKcal,'f',6)),false,18,"left",80,100,"Normal","<w:keepLines/>");
      }
  }
  ;
  const bool hasMainContent=figureCount>0||!model.stageOrder.isEmpty()||
    std::any_of(model.tables.begin(),model.tables.end(),[](const auto& table){
      return table.section!=si::DocumentSection::Validation&&!table.headers.isEmpty();
    });
  if(hasMainContent&&hasFrontMatter)body+=pageBreak;
  if(model.stageOrder.isEmpty()){for(int i=0;i<figureCount;++i)writeFigure(i);for(const auto& table:model.tables)writeTable(table);}
  else {
    for(int i=0;i<figureCount;++i)if(i>=model.figures.size()||model.figures[i].stageId.isEmpty())writeFigure(i);
    for(const auto& id:model.stageOrder){body+=heading(model.stageNames.value(id));for(int i=0;i<model.figures.size();++i)if(model.figures[i].stageId==id)writeFigure(i);for(const auto& table:model.tables)if(table.stageId==id)writeTable(table);}
    for(const auto& table:model.tables)if(table.stageId.isEmpty())writeTable(table);
  }
  QString finalSection=normalSection();
  if(!model.coordinates.isEmpty()){
    body+=heading("Cartesian Coordinates",hasMainContent||hasFrontMatter);
    body+=QStringLiteral("<w:p><w:pPr>%1</w:pPr></w:p>").arg(normalSection());
    const int fieldWidth=coordinateFieldWidth(model);
    const int columns=std::clamp(9720/(540+3*fieldWidth),1,3);
    body+=coordinateBody(model,fieldWidth);
    finalSection.replace(QStringLiteral("<w:pgSz"),QStringLiteral("<w:type w:val=\"continuous\"/><w:pgSz"));
    finalSection.replace(QStringLiteral("</w:sectPr>"),QStringLiteral("<w:cols w:num=\"%1\" w:space=\"360\"/></w:sectPr>").arg(columns));
  }
  return writePackage(path,envelope(body,finalSection),error,model.figures);
}

bool DocxWriter::writeCoordinates(const QString& path,const si::SIDocumentModel& model,QString* error){
  return writePackage(path,envelope(paragraph("Coordinates",true,24,"left",0,60,"Heading1")+coordinateBody(model,coordinateFieldWidth(model)),normalSection()),error);
}
bool DocxWriter::write(const QString& path,const QString& title,const QStringList& headers,const QVector<QStringList>& rows,QString* error){si::SIDocumentModel model;model.title=title;model.tables.append({si::DocumentSection::Other,QString(),headers,rows,{}});return writeDocument(path,model,error);}
bool DocxWriter::writeTables(const QString& path,const QString& title,const QVector<Table>& tables,QString* error){si::SIDocumentModel model;model.title=title;for(const auto& table:tables)model.tables.append({si::DocumentSection::Other,table.title,table.headers,table.rows,{}});return writeDocument(path,model,error);}
