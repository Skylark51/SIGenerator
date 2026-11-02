#include "si/XyzWriter.hpp"
#include "si/StructureLabels.hpp"
#include <QSaveFile>
#include <QTextStream>
#include <QLocale>

namespace si {
bool XyzWriter::write(const QString& path, const SIDocumentModel& model, QString* error) {
  if(model.coordinates.isEmpty()){if(error)*error="No coordinates available.";return false;}
  for(const auto& block:model.coordinates)if(block.atoms.empty()){
    if(error)*error="Final geometry missing: "+block.structureName;return false;
  }
  QSaveFile file(path);
  if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=file.errorString();return false;}
  QTextStream out(&file);out.setLocale(QLocale::c());
  for(const auto& block:model.coordinates){
    auto label=structureLabelMarkup(block.structureName);label.replace('\n',' ').replace('\r',' ');
    out<<block.atoms.size()<<'\n'<<label<<" | charge="<<(block.charge?QString::number(*block.charge):QStringLiteral("N/A"))
      <<" | multiplicity="<<(block.multiplicity?QString::number(*block.multiplicity):QStringLiteral("N/A"))
      <<" | E_SPE="<<formatNumber(block.speEnergy,8,0)<<" Eh\n";
    for(const auto& atom:block.atoms)out<<QString::fromStdString(atom.symbol)<<"    "
      <<QString::number(atom.xyz[0],'f',6)<<"    "<<QString::number(atom.xyz[1],'f',6)<<"    "<<QString::number(atom.xyz[2],'f',6)<<'\n';
  }
  out.flush();if(out.status()!=QTextStream::Ok){if(error)*error="Failed to write XYZ data.";return false;}
  if(!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
} // namespace si
