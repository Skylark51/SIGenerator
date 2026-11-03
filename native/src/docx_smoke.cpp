#include "DocxWriter.hpp"
#include "si/StructureLabels.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QtEndian>
#include <iostream>
#include <algorithm>

int main(int argc,char** argv){
  QCoreApplication app(argc,argv);QString error;bool ok=true;
  auto check=[&](bool value,const char* message){if(!value){std::cerr<<"FAILED: "<<message<<'\n';ok=false;}};
  for(const auto& example:QVector<QPair<QString,QString>>{{"1C","^{1}C"},{"5DCR","^{5}D_{CR}"},{"3DRC","^{3}D_{RC}"},{"5DCR-TS","^{5}D_{CR}-TS"},{"3DRC-TS","^{3}D_{RC}-TS"},{"12C","^{12}C"},{"DCR","D_{CR}"},{"5D_{CR}-TS","^{5}D_{CR}-TS"},{"^{5}D_{CR}-TS","^{5}D_{CR}-TS"},{"298.15 K","298.15 K"}}){
    check(si::structureLabelMarkup(example.first)==example.second,"automatic structure label markup");
    check(si::structureLabelMarkup(example.second)==example.second,"label markup is idempotent");
  }
  for(const auto& suffix:QStringList{"P1","P2","P3","HP","P1-2"}){
    for(const auto& ending:QStringList{"","-TS"}){
      const auto label="1A"+suffix+ending;
      const auto markup="^{1}A_{"+suffix+"}"+ending;
      check(si::structureLabelMarkup(label)==markup,"P/HP suffix and singlet multiplicity formatting");
      check(si::structureLabelMarkup(markup)==markup,"P/HP markup is idempotent");
      check(si::formattedTextHtml(markup)=="<sup>1</sup>A<sub>"+suffix+"</sub>"+ending,"P/HP HTML formatting");
    }
  }
  check(si::structureLabelMarkup("5BP12")=="^{5}BP12","unrequested P12 suffix remains unchanged");
  const auto inputs=si::namedStructureInputs("(5DCR-TS file (1).out) (3DRC second.out)\n(1C\tthird.out)\tplain.out");
  check(inputs.size()==4&&inputs[0].label=="5DCR-TS"&&inputs[0].fileName=="file (1).out"&&inputs[1].label=="3DRC"&&inputs[2].label=="1C"&&inputs[3].label.isEmpty(),"paired input tokenization");
  const auto excel=si::namedStructureInputs("1A\t1-CoIII-Me3-freq.out\r\n1A-TS\t1-CoIII-Me3-ts-freq.out\r\n5DCR-TS\t\"공백 있는 (계산).out\"\n3DRC    file with spaces.out");
  check(excel.size()==4&&excel[0].label=="1A"&&excel[0].fileName=="1-CoIII-Me3-freq.out"&&excel[1].label=="1A-TS"&&excel[2].fileName==QString::fromUtf8("공백 있는 (계산).out")&&excel[3].label=="3DRC"&&excel[3].fileName=="file with spaces.out","Excel two-column and whitespace pairs without parentheses");
  for(const auto& label:QStringList{"1CP1-2","5CP1-2","1-CP1-2","1CP1-2-TS","CP1-2","5BP12"}){
    for(const auto& separator:QStringList{"\t","    "}){
      const auto parsed=si::namedStructureInputs(label+separator+"reaction-ts-freq-ircfwd-opt-freq.out");
      check(parsed.size()==1&&parsed[0].label==label&&parsed[0].fileName=="reaction-ts-freq-ircfwd-opt-freq.out","hyphenated reaction labels remain attached to their filenames in TSV and shorthand input");
    }
  }
  const auto plain=si::namedStructureInputs("first.out\tsecond.log\n반응 1-ts-freq\t반응 1-ts-freq.out");
  check(plain.size()==4&&std::all_of(plain.begin(),plain.end(),[](const auto& entry){return entry.label.isEmpty();}),"ordinary multi-column filenames remain individual entries");
  check(si::formattedTextHtml(si::structureLabelMarkup("5DCR-TS"))=="<sup>5</sup>D<sub>CR</sub>-TS","HTML formatting matches requested multiplicity and suffix");
  si::SIDocumentModel model;model.manuscriptTitle="Phase 1 & manuscript";model.authors="A. Author";model.affiliations="Test Laboratory";model.correspondingEmail="author@example.org";
  model.computationalDetails="Verified computational details at 298.15 K. E_SPE + (G_freq − E_freq).";model.referencesText="Reference 1";model.figureCaptions={"Reaction profile"};
  model.tables.append({si::DocumentSection::RelativeEnergy,"Markup _x ^x _{long} ^{c}",{"Structure","E_{Total}","ΔG"},{{"A","0.00","0.00"}},{1,2}});
  for(const auto& name:{QString("1C"),QString("5DCR-TS"),QString("3DRC")})model.tables[0].rows.append({name,"0.00","0.00"});
  for(const auto& suffix:QStringList{"P1","P2","P3","HP"}){
    model.tables[0].rows.append({"1A"+suffix,"0.00","0.00"});
  }
  model.tables.append({si::DocumentSection::Validation,"Stationary-point validation of the calculated structures.",{"Structure","Imag. freq."},{{"A","0"}},{}});
  model.coordinates.append({"H_{2}O",{{8,"O",{0.0,0.0,0.0}},{1,"H",{0.9572,0.0,0.0}}}});
  for(const auto& name:{QString("1C"),QString("5DCR-TS"),QString("3DRC")})model.coordinates.append({name,model.coordinates.front().atoms});
  for(const auto& suffix:QStringList{"P1","P2","P3","HP"})model.coordinates.append({"1A"+suffix,model.coordinates.front().atoms});
  const auto output=QDir::current().filePath("si-docx-smoke.docx");
  check(DocxWriter::writeDocument(output,model,&error),"DOCX write");
  QFile file(output);check(file.open(QIODevice::ReadOnly),"read package");const auto bytes=file.readAll();
  QHash<QByteArray,QByteArray> entries;
  qsizetype offset=0;
  while(offset+30<=bytes.size()&&qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50){
    const auto* header=bytes.constData()+offset;
    const auto size=qFromLittleEndian<quint32>(header+18);
    const auto nameSize=qFromLittleEndian<quint16>(header+26),extraSize=qFromLittleEndian<quint16>(header+28);
    const auto begin=offset+30+nameSize+extraSize;
    check(begin+size<=bytes.size(),"ZIP entry bounds");if(begin+size>bytes.size())break;
    entries.insert(bytes.mid(offset+30,nameSize),bytes.mid(begin,size));offset=begin+size;
  }
  for(const auto* name:{"word/document.xml","word/styles.xml","word/footer1.xml","word/_rels/document.xml.rels","[Content_Types].xml"}){
    check(entries.contains(name),name);
  }
  for(auto it=entries.cbegin();it!=entries.cend();++it){
    QXmlStreamReader reader(it.value());while(!reader.atEnd())reader.readNext();
    check(!reader.hasError(),it.key().constData());
  }
  const auto document=entries.value("word/document.xml"),styles=entries.value("word/styles.xml"),footer=entries.value("word/footer1.xml");
  check(document.contains("<w:pStyle w:val=\"Heading1\"")&&styles.contains("w:outlineLvl w:val=\"0\""),"semantic heading and TOC outline");
  for(const auto* name:{"Normal","Title","Heading1","Heading2","Caption"})check(styles.contains(QByteArray("w:styleId=\"")+name+"\""),name);
  check(footer.contains(" PAGE ")&&footer.contains(">S</w:t>")&&document.contains("r:id=\"rIdFooter\""),"real S + PAGE field and section reference");
  check(entries.value("word/_rels/document.xml.rels").contains("Target=\"footer1.xml\"")&&entries.value("[Content_Types].xml").contains("/word/footer1.xml"),"footer relationships and content type");
  check(document.contains("Phase 1 &amp; manuscript")&&document.contains("A. Author")&&document.contains("Test Laboratory")&&document.contains("author@example.org"),"cover metadata");
  check(document.contains("Verified computational details")&&!document.contains("Stationary-point validation")&&document.contains("Coordinates")&&document.contains("Reference 1"),"legacy document content excludes validation table");
  check(!document.contains(">Contents<")&&!document.contains("List of Tables")&&document.indexOf("Computational Details")<document.indexOf("Reference 1")&&document.indexOf("Reference 1")<document.indexOf("w:type=\"page\""),"computational details and SI references on the first section");
  check(document.indexOf("Reaction profile")<document.indexOf("<w:tbl>")&&document.indexOf("</w:tbl>")<document.indexOf("Markup")&&document.indexOf(">References<")<document.indexOf(">Cartesian Coordinates<"),"legacy figures before tables and captions below tables");
  check(document.contains("w:num=\"3\"")&&document.contains("0.957200")&&document.contains("w:sz w:val=\"12\""),"legacy three-column coordinates and six decimal places");
  check(document.contains("<w:tblHeader/>")&&document.contains("<w:cantSplit/>")&&document.contains("<w:vAlign w:val=\"center\"/>")&&!document.contains("<w:shd"),"table formatting");
  check(document.count("<w:noWrap/>")==document.count("<w:tc>")&&document.contains("<w:tblLayout w:type=\"autofit\"/>"),"all table cells prohibit line wrapping and allow columns to expand");
  check(document.contains("w:val=\"subscript\"")&&document.contains("w:val=\"superscript\""),"existing caption markup retained");
  const QByteArray superscript="w:val=\"superscript\"/></w:rPr><w:t xml:space=\"preserve\">5</w:t>";
  const QByteArray subscriptCR="w:val=\"subscript\"/></w:rPr><w:t xml:space=\"preserve\">CR</w:t>";
  const QByteArray subscriptRC="w:val=\"subscript\"/></w:rPr><w:t xml:space=\"preserve\">RC</w:t>";
  check(document.count(superscript)==2&&document.count(subscriptCR)==2&&document.count(subscriptRC)==2&&document.count(">-TS</w:t>")==2,"tables and Cartesian names both use actual Word super/subscript runs and normal TS");
  for(const auto& suffix:QList<QByteArray>{"P1","P2","P3","HP"})
    check(document.count(QByteArray("w:val=\"subscript\"/></w:rPr><w:t xml:space=\"preserve\">")+suffix+"</w:t>")==2,"P/HP use Word subscript in tables and coordinates");
  QString coordinateError;const auto coordinatePath=QDir::current().filePath("coordinates-label-smoke.docx");
  check(DocxWriter::writeCoordinates(coordinatePath,model,&coordinateError),"independent labelled coordinate DOCX");QFile coordinates(coordinatePath);
  check(coordinates.open(QIODevice::ReadOnly),"read labelled coordinates DOCX");const auto coordinateBytes=coordinates.readAll();
  check(coordinateBytes.count(superscript)==1&&coordinateBytes.count(subscriptCR)==1&&coordinateBytes.count(subscriptRC)==1,"independent coordinate document uses same name formatting");
  check(!document.contains("Def2-TZVPP electronic energy =")&&!document.contains("T = 298.15 K."),"no fabricated legacy footnotes");
  si::SIDocumentModel captions;
  captions.computationalDetails="Computational Details. Computational Details\nActual computational method.";
  for(int i=0;i<12;++i){
    si::DocumentTable t{si::DocumentSection::RelativeEnergy,"Caption formatting check",{"Structure","ΔE","ΔΔE^{a}","E_{Total}","ΔZ_{0}","ΔΔE_{Thermal}^{b}","-TΔS^{b}"},{},{}};
    const bool dispersion=(i%4)&1,complexation=(i%4)&2;
    if(dispersion)t.headers.append("Δ_{Dispersion}");if(complexation)t.headers.append("Δ_{Complexation}");t.headers.append("ΔG^{c}");
    QStringList row{"1A"};while(row.size()<t.headers.size())row.append("0.00");t.rows.append(row);captions.tables.append(t);
    const QString word=dispersion&&complexation?"six":dispersion||complexation?"five":"four";
    check(si::relativeGibbsFootnote(t)==QString("Gibbs free energy = sum of the previous %1 columns, starting with E_{Total}.").arg(word),"Gibbs footnote counts only selected contributing columns");
  }
  const auto captionPath=QDir::current().filePath("caption-footnote-smoke.docx");
  check(DocxWriter::writeDocument(captionPath,captions,&error),"write caption and footnote regression document");
  QFile captionFile(captionPath);check(captionFile.open(QIODevice::ReadOnly),"read caption regression document");const auto captionPackage=captionFile.readAll();
  check(captionPackage.count("sum of the previous four columns")==3&&captionPackage.count("sum of the previous five columns")==6&&captionPackage.count("sum of the previous six columns")==3,"exported footnotes use four, five or six according to actual selected corrections");
  check(captionPackage.count("Computational Details")==1&&captionPackage.contains("Actual computational method."),"export renders the computational heading exactly once");
  QXmlStreamReader captionXml;
  for(qsizetype at=0;at+30<captionPackage.size()&&qFromLittleEndian<quint32>(captionPackage.constData()+at)==0x04034b50;){
    const auto* h=captionPackage.constData()+at;const auto size=qFromLittleEndian<quint32>(h+18);const auto n=qFromLittleEndian<quint16>(h+26),extra=qFromLittleEndian<quint16>(h+28);const auto begin=at+30+n+extra;
    if(captionPackage.mid(at+30,n)=="word/document.xml")captionXml.addData(captionPackage.mid(begin,size));at=begin+size;
  }
  int numbers=0,instructions=0;bool caption=false,bold=false;
  while(!captionXml.atEnd()){
    captionXml.readNext();if(!captionXml.isStartElement())continue;const auto tag=captionXml.name();
    if(tag=="p")caption=false;else if(tag=="r")bold=false;else if(tag=="b")bold=true;
    else if(tag=="instrText"){
      const auto instruction=captionXml.readElementText();if(instruction.contains("SEQ Table_S")){++instructions;check(bold&&instruction.contains("CHARFORMAT"),"SEQ instruction is bold and preserves character format on Word updates");}
    }else if(tag=="t"){
      const auto s=captionXml.readElementText();if(s=="Table S")caption=true;
      else if(caption&&QRegularExpression("^\\d+$").match(s).hasMatch()){++numbers;check(bold,"every digit of single and double digit table caption numbers is bold");}
    }
  }
  check(!captionXml.hasError()&&numbers==12&&instructions==12,"all twelve table sequence fields have bold instructions and results");
  std::cout<<(ok?"native DOCX smoke: PASS\n":"native DOCX smoke: FAIL\n");return ok?0:1;
}
