#include "si/XyzWriter.hpp"
#include "si/StructureLabels.hpp"
#include <QSaveFile>
#include <QTextStream>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace si {
bool XyzWriter::write(const QString& path, const SIDocumentModel& model, QString* error) {
  if(model.coordinates.isEmpty()){if(error)*error="No coordinates available.";return false;}
  for(const auto& block:model.coordinates){
    if(block.atoms.empty()){
      if(error)*error="Final geometry missing: "+block.structureName;return false;
    }
    if(std::any_of(block.atoms.begin(),block.atoms.end(),[](const Atom& atom){
      return atom.atomicNumber<=0||atom.symbol.empty()||
        !std::all_of(atom.xyz.begin(),atom.xyz.end(),[](double value){return std::isfinite(value);});
    })){
      if(error)*error="Invalid XYZ atom or coordinate: "+block.structureName;return false;
    }
  }
  QSaveFile file(path);
  if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=file.errorString();return false;}
  QTextStream out(&file);out.setLocale(QLocale::c());
  for(const auto& block:model.coordinates){
    auto label=structureLabelMarkup(block.structureName);label.replace('\n',' ').replace('\r',' ');
    out<<block.atoms.size()<<'\n'<<label<<" | charge="<<(block.charge?QString::number(*block.charge):QStringLiteral("N/A"))
      <<" | multiplicity="<<(block.multiplicity?QString::number(*block.multiplicity):QStringLiteral("N/A"))
      <<" | E_SPE="<<formatNumber(block.speEnergy,8,0)<<" Eh\n";
    // Write every atom of every frame with round-trip double precision.
    for(const auto& atom:block.atoms)out<<QString::fromStdString(atom.symbol)<<"    "
      <<QString::number(atom.xyz[0],'g',17)<<"    "<<QString::number(atom.xyz[1],'g',17)<<"    "<<QString::number(atom.xyz[2],'g',17)<<'\n';
  }
  out.flush();if(out.status()!=QTextStream::Ok){if(error)*error="Failed to write XYZ data.";return false;}
  if(!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
} // namespace si
