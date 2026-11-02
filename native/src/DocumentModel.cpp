#include "si/DocumentModel.hpp"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace si {

bool SIDocumentModel::empty() const {
  return tables.isEmpty() && figures.isEmpty() && figureCaptions.isEmpty() && coordinates.isEmpty();
}

int SIDocumentModel::tableCount() const { return tables.size(); }

int SIDocumentModel::rowCount(DocumentSection section) const {
  if (const auto* found = table(section)) return found->rows.size();
  return 0;
}

const DocumentTable* SIDocumentModel::table(DocumentSection section) const {
  for (const auto& value : tables) if (value.section == section) return &value;
  return nullptr;
}

QString displayMechanism(const SIDocumentModel& model) {
  return model.mechanismName.trimmed().isEmpty() ? QStringLiteral("the mechanism") : model.mechanismName.trimmed();
}

QString relativeGibbsFootnote(const DocumentTable& table) {
  const int first=table.headers.indexOf(QStringLiteral("E_{Total}"));
  const int last=table.headers.indexOf(QStringLiteral("ΔG^{c}"));
  if(first<0||last<=first)return {};
  // ΔE and ΔΔE are already included in E_Total. Count only the terms
  // actually summed after the optional correction columns are filtered.
  const int count=last-first;
  const QString word=count==4?QStringLiteral("four"):count==5?QStringLiteral("five"):
    count==6?QStringLiteral("six"):QString::number(count);
  return QStringLiteral("Gibbs free energy = sum of the previous %1 columns, starting with E_{Total}.").arg(word);
}

QString computationalDetailsBody(QString text) {
  static const QRegularExpression heading(QStringLiteral("^\\s*(?:Computational\\s+Details(?:[.:]\\s*|[\\r\\n]+|$))+"),QRegularExpression::CaseInsensitiveOption);
  text.remove(heading);return text.trimmed();
}

QString formatNumber(double value, int defaultDigits, int precisionOverride) {
  if(!std::isfinite(value))return QStringLiteral("N/A");
  return QString::number(value, 'f', precisionOverride > 0 ? precisionOverride : defaultDigits);
}

QString formatNumber(const std::optional<double>& value, int defaultDigits, int precisionOverride) {
  return value ? formatNumber(*value, defaultDigits, precisionOverride) : QStringLiteral("N/A");
}


QString computationalDetails(const std::vector<EnergyRecord>& records, EnergyBasis basis, QStringList* warnings) {
  if(records.empty())return {};
  QStringList sentences;
  auto uniform=[&](const std::vector<const GaussianResult*>& sources,auto getter,const QString& label){
    QSet<QString> values;bool missing=false;
    for(const auto* source:sources){const auto value=getter(*source);if(value.isEmpty())missing=true;else values.insert(value);}
    if(values.size()>1||(missing&&!values.isEmpty())){
      if(warnings)warnings->append(label+" differs between structures or is incompletely reported");
      return QString();
    }
    return !missing&&values.size()==1?*values.begin():QString();
  };
  std::vector<const GaussianResult*> freq,spe;
  for(const auto& record:records){freq.push_back(&record.frequency);if(record.singlePoint&&record.singlePoint->scfEnergy)spe.push_back(&*record.singlePoint);}
  auto allSources=freq;allSources.insert(allSources.end(),spe.begin(),spe.end());
  auto program=[](const GaussianResult& r){if(!r.program||!r.version)return QString();return QString::fromStdString(*r.program+" "+*r.version+(r.revision?" Revision "+*r.revision:""));};
  const auto software=uniform(allSources,program,"Program/version");
  if(!software.isEmpty())sentences.append("Calculations were performed using "+software+".");
  auto level=[](const GaussianResult& r){return r.method.empty()||r.basis.empty()?QString():QString::fromStdString(r.method+"/"+r.basis);};
  const auto freqLevel=uniform(freq,level,"Primary calculation level");
  const bool allFreq=std::all_of(freq.begin(),freq.end(),[](auto* r){return r->frequenciesPresent;});
  const bool allOpt=std::all_of(freq.begin(),freq.end(),[](auto* r){return std::find(r->jobTypes.begin(),r->jobTypes.end(),"opt")!=r->jobTypes.end();});
  if(!freqLevel.isEmpty()){
    if(allFreq&&allOpt)sentences.append("Geometry optimizations and vibrational frequency calculations were performed at the "+freqLevel+" level.");
    else if(allFreq)sentences.append("Vibrational frequency calculations were performed at the "+freqLevel+" level.");
    else sentences.append("Primary calculations used the "+freqLevel+" level.");
  }
  auto routeToken=[](const GaussianResult& r,const QString& key){
    const QRegularExpression re("\\b"+key+"\\s*(?:=\\s*)?(\\([^)]*\\)|[^\\s]+)",QRegularExpression::CaseInsensitiveOption);
    const auto match=re.match(QString::fromStdString(r.route));
    return match.hasMatch()?match.captured(0).simplified().toLower():QString();
  };
  for(const auto& key:{QStringLiteral("scrf"),QStringLiteral("empiricaldispersion")}){
    const auto keyword=uniform(freq,[&](const auto& r){return routeToken(r,key);},key=="scrf"?"Primary solvent":"Primary dispersion");
    if(!keyword.isEmpty())sentences.append("The primary calculation route specified "+keyword+".");
    if(!spe.empty()){
      const auto speKeyword=uniform(spe,[&](const auto& r){return routeToken(r,key);},key=="scrf"?"SPE solvent":"SPE dispersion");
      if(!speKeyword.isEmpty())sentences.append("The single-point route specified "+speKeyword+".");
    }
  }
  if(!spe.empty()){
    const auto speLevel=uniform(spe,level,"Single-point level");
    if(!speLevel.isEmpty())sentences.append(QString("Electronic energies %1were refined by single-point calculations at the %2 level.").arg(spe.size()==records.size()?QString():QString("for structures with paired SPE files "),speLevel));
  }
  int zero=0,one=0,many=0,missing=0;
  for(auto* r:freq){if(!r->frequenciesPresent)++missing;else if(r->imaginaryFrequencies.empty())++zero;else if(r->imaginaryFrequencies.size()==1)++one;else ++many;}
  if(zero+one+many)sentences.append(QString("Frequency analysis found %1 structures with zero, %2 with one, and %3 with two or more imaginary frequencies.").arg(zero).arg(one).arg(many));
  if(missing)sentences.append(QString("Frequency data were unavailable for %1 structures.").arg(missing));
  const auto temperature=uniform(freq,[](const GaussianResult& r){return r.temperatureKelvin?QString::number(*r.temperatureKelvin,'g',12):QString();},"Temperature");
  if(!temperature.isEmpty()&&allFreq)sentences.append("Thermal corrections were obtained from the frequency calculations at "+temperature+" K.");
  if(basis==EnergyBasis::DefaultComposite||basis==EnergyBasis::Gibbs){
    sentences.append("Composite Gibbs free energies were calculated as E_SPE + (G_freq − E_freq), where E_freq and G_freq are the SCF energy and the sum of electronic and thermal free energies from the frequency calculation. Reported dispersion energies were not added again.");
    if(spe.size()!=records.size())sentences.append("For structures without a paired SPE energy, G_freq was used.");
  }else if(basis==EnergyBasis::LegacyComposite){
    sentences.append("Relative energies were assembled using the original SI component formulas: electronic energy differences plus ZPE, additional thermal, entropy, and included correction terms (T = 298.15 K).");
  }else if(basis==EnergyBasis::Electronic)sentences.append("Relative energies were calculated from electronic energies, using paired SPE energies where available.");
  else if(basis==EnergyBasis::ElectronicZpe)sentences.append("Relative energies include the electronic energy and the frequency calculation's zero-point correction.");
  else if(basis==EnergyBasis::Enthalpy)sentences.append("Enthalpies were obtained from the primary calculation, with E_SPE − E_freq added where a paired SPE energy was available.");
  if(warnings&&!warnings->isEmpty())sentences.append("Calculation conditions differ or are incompletely reported across structures; the individual source outputs should be consulted.");
  return sentences.join(' ');
}

DocumentTable validationTable(const std::vector<EnergyRecord>& records){
  DocumentTable table{DocumentSection::Validation,"Stationary-point validation of the calculated structures.",
    {"Structure","Charge","Multiplicity","Normal termination","Imag. freq.","Imaginary frequency (cm−1)","⟨S²⟩ before","⟨S²⟩ after"},{},{}};
  for(const auto& record:records){
    const auto& r=record.frequency;QStringList imaginary;
    for(const auto value:r.imaginaryFrequencies)imaginary.append(formatNumber(value,2,0));
    table.rows.append({QString::fromStdString(record.name),r.charge?QString::number(*r.charge):QStringLiteral("N/A"),
      r.multiplicity?QString::number(*r.multiplicity):QStringLiteral("N/A"),r.normalTermination?QStringLiteral("Yes"):QStringLiteral("N/A"),
      r.frequenciesPresent?QString::number(r.imaginaryFrequencies.size()):QStringLiteral("N/A"),
      !r.frequenciesPresent?QStringLiteral("N/A"):imaginary.isEmpty()?QStringLiteral("None"):imaginary.join(", "),
      formatNumber(r.sSquaredBefore,3,0),formatNumber(r.sSquaredAfter,3,0)});
  }
  return table;
}

} // namespace si
