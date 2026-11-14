#include "GaussianSnapshot.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include <utility>

namespace si {
namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
std::string bytes(const QJsonValue& value) { return value.toString().toStdString(); }
QString pathText(const std::filesystem::path& path) { return QString::fromStdWString(path.wstring()); }
std::filesystem::path pathValue(const QJsonValue& value) { return std::filesystem::path(value.toString().toStdWString()); }

void add(QJsonObject& object, const char* key, const std::optional<double>& value) {
  if (value) object.insert(QLatin1String(key), *value);
}
void add(QJsonObject& object, const char* key, const std::optional<int>& value) {
  if (value) object.insert(QLatin1String(key), *value);
}
void add(QJsonObject& object, const char* key, const std::optional<std::string>& value) {
  if (value) object.insert(QLatin1String(key), text(*value));
}
std::optional<double> optionalNumber(const QJsonObject& object, const char* key) {
  const auto value = object.value(QLatin1String(key));
  if (value.isDouble()) return value.toDouble();
  return {};
}
std::optional<int> optionalInteger(const QJsonObject& object, const char* key) {
  const auto value = object.value(QLatin1String(key));
  if (value.isDouble()) return value.toInt();
  return {};
}
std::optional<std::string> optionalText(const QJsonObject& object, const char* key) {
  const auto value = object.value(QLatin1String(key));
  if (value.isString()) return bytes(value);
  return {};
}
QJsonValue value(const std::optional<double>& number) {
  return number ? QJsonValue(*number) : QJsonValue(QJsonValue::Null);
}
std::optional<double> optionalNumber(const QJsonValue& number) {
  return number.isDouble() ? std::optional<double>(number.toDouble()) : std::nullopt;
}
QJsonArray textArray(const std::vector<std::string>& entries) {
  QJsonArray result;
  for (const auto& entry : entries) result.append(text(entry));
  return result;
}
std::vector<std::string> readTextArray(const QJsonValue& value) {
  std::vector<std::string> result;
  for (const auto& entry : value.toArray()) if (entry.isString()) result.push_back(bytes(entry));
  return result;
}
}

QJsonObject GaussianSnapshot::encode(const GaussianResult& result) {
  QJsonObject data{
    {"path",pathText(result.path)}, {"success",result.success},
    {"reason",text(result.reason)}, {"warnings",textArray(result.warnings)},
    {"frequency_present",result.frequenciesPresent}, {"normal_termination",result.normalTermination},
    {"spin_source",pathText(result.spinSourcePath)}, {"spin_basis",text(result.spinBasisLevel)},
    {"spin_fallback",result.spinFallback}, {"route",text(result.route)},
    {"method",text(result.method)}, {"basis",text(result.basis)},
    {"job_type",text(result.jobType)}, {"basis_level",text(result.basisLevel)},
    {"job_types",textArray(result.jobTypes)}
  };
  add(data,"scf",result.scfEnergy);
  add(data,"zpe",result.zeroPointCorrection);
  add(data,"thermal",result.thermalCorrection);
  add(data,"thermal_gibbs",result.thermalGibbsCorrection);
  add(data,"enthalpy",result.enthalpy);
  add(data,"gibbs",result.gibbsFreeEnergy);
  add(data,"entropy",result.entropy);
  add(data,"dispersion",result.dispersion);
  add(data,"zpe_kcal",result.zeroPointKcal);
  add(data,"thermal_kcal",result.thermalTotalKcal);
  add(data,"program",result.program);
  add(data,"version",result.version);
  add(data,"revision",result.revision);
  add(data,"temperature",result.temperatureKelvin);
  add(data,"pressure",result.pressureAtm);
  add(data,"s2_before",result.sSquaredBefore);
  add(data,"s2_after",result.sSquaredAfter);
  add(data,"charge",result.charge);
  add(data,"multiplicity",result.multiplicity);

  QJsonArray frequencies;
  for (double frequency : result.imaginaryFrequencies) frequencies.append(frequency);
  data.insert("imaginary",frequencies);
  QJsonArray modes;
  for(double frequency:result.vibrationalFrequencies)modes.append(frequency);
  data.insert("vibrational_frequencies",modes);

  QJsonArray atoms;
  for (const auto& atom : result.finalGeometry)
    atoms.append(QJsonArray{atom.atomicNumber,text(atom.symbol),atom.xyz[0],atom.xyz[1],atom.xyz[2]});
  data.insert("atoms",atoms);

  QJsonArray spins;
  for (const auto& spin : result.spinDensities)
    spins.append(QJsonArray{spin.atomIndex,text(spin.atom),spin.charge,spin.spin});
  data.insert("spins",spins);

  QJsonArray excited;
  for (const auto& state : result.excitedStates)
    excited.append(QJsonArray{state.index,state.energyEv,value(state.wavelengthNm),
                              value(state.oscillatorStrength),text(state.description)});
  data.insert("excited",excited);

  QJsonArray irc;
  for (const auto& point : result.ircPoints)
    irc.append(QJsonArray{point.index,text(point.direction),value(point.scfEnergy)});
  data.insert("irc",irc);

  const auto json = QJsonDocument(data).toJson(QJsonDocument::Compact);
  const auto compressed = qCompress(json,9);
  return {{"format",QStringLiteral("gaussian-extracted-v1")},
          {"sha256",QString::fromLatin1(QCryptographicHash::hash(json,QCryptographicHash::Sha256).toHex())},
          {"data",QString::fromLatin1(compressed.toBase64())}};
}

bool GaussianSnapshot::decode(const QJsonObject& snapshot, GaussianResult* result) {
  if (!result || snapshot.value("format").toString()!=QStringLiteral("gaussian-extracted-v1"))
    return false;
  const auto base64=snapshot.value("data").toString().toLatin1();
  if (base64.isEmpty()) return false;
  const auto json=qUncompress(QByteArray::fromBase64(base64));
  if (json.isEmpty()) return false;
  const auto digest=QString::fromLatin1(QCryptographicHash::hash(json,QCryptographicHash::Sha256).toHex());
  if (digest!=snapshot.value("sha256").toString()) return false;
  const auto document=QJsonDocument::fromJson(json);
  if (!document.isObject()) return false;
  const auto data=document.object();
  if (!data.value("atoms").isArray() || !data.value("spins").isArray()) return false;

  GaussianResult restored;
  restored.path=pathValue(data.value("path"));
  restored.success=data.value("success").toBool(true);
  restored.reason=bytes(data.value("reason"));
  restored.warnings=readTextArray(data.value("warnings"));
  restored.scfEnergy=optionalNumber(data,"scf");
  restored.zeroPointCorrection=optionalNumber(data,"zpe");
  restored.thermalCorrection=optionalNumber(data,"thermal");
  restored.thermalGibbsCorrection=optionalNumber(data,"thermal_gibbs");
  restored.enthalpy=optionalNumber(data,"enthalpy");
  restored.gibbsFreeEnergy=optionalNumber(data,"gibbs");
  restored.entropy=optionalNumber(data,"entropy");
  restored.dispersion=optionalNumber(data,"dispersion");
  restored.zeroPointKcal=optionalNumber(data,"zpe_kcal");
  restored.thermalTotalKcal=optionalNumber(data,"thermal_kcal");
  restored.program=optionalText(data,"program");
  restored.version=optionalText(data,"version");
  restored.revision=optionalText(data,"revision");
  restored.temperatureKelvin=optionalNumber(data,"temperature");
  restored.pressureAtm=optionalNumber(data,"pressure");
  restored.sSquaredBefore=optionalNumber(data,"s2_before");
  restored.sSquaredAfter=optionalNumber(data,"s2_after");
  restored.charge=optionalInteger(data,"charge");
  restored.multiplicity=optionalInteger(data,"multiplicity");
  restored.frequenciesPresent=data.value("frequency_present").toBool();
  restored.normalTermination=data.value("normal_termination").toBool();
  restored.spinSourcePath=pathValue(data.value("spin_source"));
  restored.spinBasisLevel=bytes(data.value("spin_basis"));
  restored.spinFallback=data.value("spin_fallback").toBool();
  restored.route=bytes(data.value("route"));
  restored.method=bytes(data.value("method"));
  restored.basis=bytes(data.value("basis"));
  restored.jobType=bytes(data.value("job_type"));
  restored.basisLevel=bytes(data.value("basis_level"));
  restored.jobTypes=readTextArray(data.value("job_types"));

  for (const auto& entry : data.value("imaginary").toArray())
    if (entry.isDouble()) restored.imaginaryFrequencies.push_back(entry.toDouble());
  for (const auto& entry : data.value("vibrational_frequencies").toArray())
    if (entry.isDouble()) restored.vibrationalFrequencies.push_back(entry.toDouble());
  for (const auto& entry : data.value("atoms").toArray()) {
    const auto atom=entry.toArray();
    if (atom.size()!=5 || !atom[0].isDouble() || !atom[1].isString()) return false;
    restored.finalGeometry.push_back({atom[0].toInt(),bytes(atom[1]),
                                      {atom[2].toDouble(),atom[3].toDouble(),atom[4].toDouble()}});
  }
  for (const auto& entry : data.value("spins").toArray()) {
    const auto spin=entry.toArray();
    if (spin.size()!=4 || !spin[0].isDouble() || !spin[1].isString()) return false;
    restored.spinDensities.push_back({spin[0].toInt(),bytes(spin[1]),
                                      spin[2].toDouble(),spin[3].toDouble()});
  }
  for (const auto& entry : data.value("excited").toArray()) {
    const auto excited=entry.toArray();
    if (excited.size()!=5) return false;
    restored.excitedStates.push_back({excited[0].toInt(),excited[1].toDouble(),
      optionalNumber(excited[2]),optionalNumber(excited[3]),bytes(excited[4])});
  }
  for (const auto& entry : data.value("irc").toArray()) {
    const auto point=entry.toArray();
    if (point.size()!=3) return false;
    restored.ircPoints.push_back({point[0].toInt(),bytes(point[1]),optionalNumber(point[2])});
  }
  *result=std::move(restored);
  return true;
}

} // namespace si
