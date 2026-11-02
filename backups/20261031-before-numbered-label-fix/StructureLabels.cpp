#include "si/StructureLabels.hpp"
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace si {
namespace {
QString unquote(QString value) {
  value=value.trimmed();
  if(value.size()>=2&&value.front()=='"'&&value.back()=='"')value=value.mid(1,value.size()-2).trimmed();
  return value;
}
QStringList separatePairs(const QString& cell) {
  QStringList pairs;int depth=0,start=-1;bool quoted=false;
  for(int i=0;i<cell.size();++i){
    const auto c=cell[i];
    if(c=='"'){quoted=!quoted;continue;}
    if(quoted)continue;
    if(c=='('){if(depth++==0)start=i;}
    else if(c==')'){
      if(depth<=0)return {cell};
      if(--depth==0)pairs.append(cell.mid(start,i-start+1));
    }else if(depth==0&&!c.isSpace())return {cell};
  }
  return depth==0&&pairs.size()>1?pairs:QStringList{cell};
}
}
QVector<NamedStructureInput> namedStructureInputs(const QString& text) {
  QVector<NamedStructureInput> result;
  static const QRegularExpression pair(QStringLiteral("^\\(\\s*(\\S+)\\s+(.+?)\\s*\\)$"));
  static const QRegularExpression labelToken(QStringLiteral("^(?:\\d+[A-Za-z][A-Za-z0-9]*(?:-TS)?|[A-Z][A-Z0-9]*(?:-TS)?|\\^\\{\\d+\\}\\S+)$"));
  static const QRegularExpression spacedPair(QStringLiteral("^(\\S+)\\s+(.+)$"));
  // Keep spreadsheet rows intact until deciding whether their two columns
  // form a label/file pair. Splitting all tabs first loses that relationship.
  for(const auto& line:text.split(QRegularExpression("[\\r\\n]+"),Qt::SkipEmptyParts)){
    QStringList cells;QString cell;int depth=0;bool quoted=false;
    auto flush=[&]{cells.append(unquote(cell));cell.clear();};
    for(const auto c:line){
      if(c=='\t'&&depth==0&&!quoted){flush();continue;}
      if(c=='"')quoted=!quoted;
      if(!quoted){if(c=='(')++depth;else if(c==')')depth=std::max(0,depth-1);}
      cell+=c;
    }
    flush();
    while(!cells.isEmpty()&&cells.front().isEmpty())cells.removeFirst();
    while(!cells.isEmpty()&&cells.back().isEmpty())cells.removeLast();
    if(cells.size()==2&&labelToken.match(cells[0]).hasMatch()&&!cells[1].isEmpty()){
      result.append({line.trimmed(),cells[1],cells[0],true});continue;
    }
    for(const auto& raw:cells)for(const auto& value:separatePairs(raw)){
      if(value.trimmed().isEmpty())continue;
      auto match=pair.match(value);
      if(match.hasMatch()){result.append({value,unquote(match.captured(2)),match.captured(1)});continue;}
      match=spacedPair.match(value);
      if(match.hasMatch()&&labelToken.match(match.captured(1)).hasMatch())
        result.append({value,unquote(match.captured(2)),match.captured(1),true});
      else result.append({value,unquote(value),{}});
    }
  }
  return result;
}
QString structureLabelMarkup(QString label) {
  static const QRegularExpression multiplicity(QStringLiteral("^(\\d+)(?=[A-Za-z])"));
  auto match=multiplicity.match(label);
  if(match.hasMatch())label.replace(0,match.capturedLength(),QStringLiteral("^{%1}").arg(match.captured(1)));
  static const QRegularExpression suffix(QStringLiteral("(?<=[A-Za-z0-9}])(CR|RC)(?=-TS$|$)"));
  match=suffix.match(label);
  if(match.hasMatch())label.replace(match.capturedStart(),match.capturedLength(),QStringLiteral("_{%1}").arg(match.captured(1)));
  return label;
}
QString formattedTextHtml(const QString& text) {
  static const QRegularExpression re(QStringLiteral("([_^])(?:\\{([^{}]+)\\}|([IVX]+|[-+]?\\d+|[^_^\\s]))"));
  QString result;int offset=0;auto matches=re.globalMatch(text);
  while(matches.hasNext()){
    const auto match=matches.next();result+=text.mid(offset,match.capturedStart()-offset).toHtmlEscaped();
    const auto tag=match.captured(1)=="_"?QStringLiteral("sub"):QStringLiteral("sup");
    result+=QStringLiteral("<%1>%2</%1>").arg(tag,(match.captured(2).isEmpty()?match.captured(3):match.captured(2)).toHtmlEscaped());
    offset=match.capturedEnd();
  }
  result+=text.mid(offset).toHtmlEscaped();return result.replace('\n',QStringLiteral("<br>"));
}
}
