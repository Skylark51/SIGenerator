#include "si/GaussianParser.hpp"
#include "si/SIModel.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <cmath>
#include <sstream>
#include <string_view>

namespace si {
namespace {
std::string trim(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) return {};
  return std::string(value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1));
}
std::string lower(std::string value) { for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return value; }
bool contains(std::string_view value, std::string_view needle) { return value.find(needle) != std::string_view::npos; }
std::optional<double> parseNumber(std::string value);
std::optional<double> numberAfter(std::string_view line, std::string_view marker) {
  const auto pos = line.find(marker); if (pos == std::string_view::npos) return std::nullopt;
  std::string value(trim(line.substr(pos + marker.size())));
  for (char& c : value) if (c == 'D' || c == 'd') c = 'E';
  const auto start=value.find_first_of("+-0123456789."); if(start==std::string::npos)return std::nullopt; char* end{}; const double parsed = std::strtod(value.c_str()+start, &end);
  return end != value.c_str()+start && std::isfinite(parsed) ? std::optional<double>{parsed} : std::nullopt;
}
std::optional<double> numberBefore(std::string_view line, std::string_view marker) {
  const auto pos=line.find(marker); if(pos==std::string_view::npos) return std::nullopt; const auto prefix=trim(line.substr(0,pos)); const auto split=prefix.find_last_of(" \t"); return parseNumber(split==std::string::npos?prefix:prefix.substr(split+1));
}
std::vector<std::string> words(std::string_view line) { std::istringstream in{std::string(line)}; std::vector<std::string> out; for (std::string item; in >> item;) out.push_back(std::move(item)); return out; }
bool dashed(std::string_view line) { const auto text = trim(line); return text.size() >= 3 && std::all_of(text.begin(), text.end(), [](unsigned char c){ return c == '-'; }); }
std::optional<double> parseNumber(std::string value) { for (char& c : value) if (c == 'D' || c == 'd') c = 'E'; char* end{}; const auto parsed = std::strtod(value.c_str(), &end); return end != value.c_str() && *end=='\0' && std::isfinite(parsed) ? std::optional<double>{parsed} : std::nullopt; }
}

std::filesystem::path pairedSinglePointPath(const std::filesystem::path& path){
  const auto utf8=path.filename().u8string();const std::string name(utf8.begin(),utf8.end());const auto low=lower(name);
  for(const auto& extension:{std::string(".out"),std::string(".log")}){
    const auto suffix="freq"+extension;
    if(low.ends_with(suffix)){
      const auto candidate=path.parent_path()/std::filesystem::u8path(name.substr(0,name.size()-suffix.size())+"freq-bb"+extension);
      if(std::filesystem::exists(candidate))return candidate;
    }
  }
  return {};
}

std::string GaussianParser::elementSymbol(int number) {
  static constexpr std::array symbols{"", "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn"};
  return number > 0 && static_cast<size_t>(number) < symbols.size() ? symbols[number] : std::to_string(number);
}

GaussianResult GaussianParser::parse(const std::filesystem::path& path, bool includePairedSpin) const {
  GaussianResult result; result.path = path;
  std::ifstream input(path, std::ios::binary);
  if (!input) { result.success = false; result.reason = "cannot open file"; return result; }
  std::string line, route, activeRoute, activeIrcDirection; bool collectingRoute = false, inOrientation = false, inSpin = false, hydrogenSummedSpin = false, restrictedScf = false;
  std::optional<int> activeIrcIndex;
  int orientationDashes = 0; std::vector<Atom> geometry, lastGeometry;
  std::vector<SpinDensity> spins, fullSpins, summedSpins;
  const auto finishSpinBlock=[&]{
    if(spins.empty())return;
    if(hydrogenSummedSpin)summedSpins=spins;
    else fullSpins=spins;
  };
  bool entropyTable=false, readingZeroPointKcal=false;
  std::vector<double> normalFrequencies, highPrecisionFrequencies;
  bool sawNormalFrequencies=false;
  static const std::regex versionPattern(R"(Gaussian\s+(\d{2})(?:\s*:\s*[^\s,]+)?\s*[, ]*\s*(?:Revision\s+|G\d{2}Rev)([A-Za-z]\.\d+))",std::regex::icase);
  static const std::regex programPattern(R"(Gaussian\s+(\d{2})\s*[:,])",std::regex::icase);
  static const std::regex compactRevision(R"(G(\d{2})Rev([A-Za-z]\.\d+))",std::regex::icase);
  while (std::getline(input, line)) {
    const auto clean = trim(line); const auto low = lower(clean);
    // A normal termination from an earlier Link1 job does not certify the last job.
    if(result.normalTermination &&
       ((!clean.empty()&&clean.front()=='#') ||
        contains(low,"entering gaussian system") ||
        contains(low,"entering link 1 =") ||
        contains(low,"--link1--")))
      result.normalTermination=false;
    if (!clean.empty() && clean.front() == '#') { collectingRoute = true; activeRoute = clean; }
    else if (collectingRoute && clean.empty()) { route = activeRoute; collectingRoute = false; }
    else if (collectingRoute) { activeRoute += " " + clean; }
    std::smatch metadata;
    // Version banners are rare. Running three regex engines on every output
    // line dominates large imports (and can exhaust the regex stack on long lines).
    if(contains(low,"gaussian") || contains(low,"rev")) {
    if(std::regex_search(line,metadata,versionPattern)){
      result.program="Gaussian";result.version=metadata[1].str();result.revision=metadata[2].str();
    }else if(std::regex_search(line,metadata,compactRevision)){
      result.program="Gaussian";result.version=metadata[1].str();result.revision=metadata[2].str();
    }else if(std::regex_search(line,metadata,programPattern)){
      result.program="Gaussian";result.version=metadata[1].str();
    }
    }
    if(low.starts_with("temperature")&&contains(low,"kelvin"))
      result.temperatureKelvin=numberAfter(low,"temperature");
    if(contains(low,"pressure")&&contains(low,"atm"))
      result.pressureAtm=numberAfter(low,"pressure");
    if(contains(low,"s**2 before annihilation")){
      const auto after=low.find("after");
      result.sSquaredBefore=numberAfter(low.substr(0,after),"s**2 before annihilation");
      if(after!=std::string::npos)result.sSquaredAfter=numberAfter(low.substr(after),"after");
    }
    if(contains(low,"cal/mol-kelvin"))entropyTable=true;
    if(entropyTable&&clean.starts_with("Total")){
      const auto columns=words(clean);
      if(columns.size()==4){result.thermalTotalKcal=parseNumber(columns[1]);result.entropy=parseNumber(columns.back());}
      entropyTable=false;
    }
    if(contains(low,"r6disp"))result.dispersion=numberAfter(low,"r6disp");
    if (contains(low, "normal termination")) result.normalTermination = true;
    if (contains(line, "SCF Done:")) { result.scfEnergy = numberAfter(line, "=");
      const auto marker=line.find("E(");
      if(marker!=std::string::npos&&marker+2<line.size())
        restrictedScf=(line[marker+2]=='R');
      if(activeIrcIndex && result.scfEnergy) { result.ircPoints.push_back({*activeIrcIndex,activeIrcDirection,result.scfEnergy}); activeIrcIndex.reset(); } }
    if (contains(line, "Zero-point correction=")) result.zeroPointCorrection = numberAfter(line, "=");
    if (contains(low,"zero-point vibrational energy")) {
      result.zeroPointKcal=numberBefore(low,"(kcal/mol)");
      readingZeroPointKcal=!result.zeroPointKcal;
    } else if(readingZeroPointKcal&&!clean.empty()) {
      result.zeroPointKcal=numberBefore(low,"(kcal/mol)");
      readingZeroPointKcal=false;
    }
    if (contains(line, "Thermal correction to Energy=")) result.thermalCorrection = numberAfter(line, "=");
    if (contains(line, "Thermal correction to Gibbs Free Energy=")) result.thermalGibbsCorrection = numberAfter(line, "=");
    if (contains(low, "total entropy")) result.entropy = numberAfter(low, "total entropy");
    if (contains(low, "edisp")) result.dispersion = numberAfter(low, "edisp");
    if (contains(low, "dispersion energy")) result.dispersion = numberAfter(low, "dispersion energy");
    if (contains(line, "Sum of electronic and thermal Enthalpies=")) result.enthalpy = numberAfter(line, "=");
    if (contains(line, "Sum of electronic and thermal Free Energies=")) result.gibbsFreeEnergy = numberAfter(line, "=");
    if (contains(line, "Charge =") && contains(line, "Multiplicity =")) { const auto items = words(line); for (size_t i = 0; i + 2 < items.size(); ++i) { if (items[i] == "Charge" && items[i + 1] == "=") result.charge = std::stoi(items[i + 2]); if (items[i] == "Multiplicity" && items[i + 1] == "=") result.multiplicity = std::stoi(items[i + 2]); } }
    // Gaussian can print both high-precision and ordinary modes for one job,
    // and several harmonic analyses in one output. Keep the final analysis and
    // prefer its ordinary block instead of counting the same modes twice.
    if (low.starts_with("harmonic frequencies")) {
      normalFrequencies.clear();highPrecisionFrequencies.clear();sawNormalFrequencies=false;
    }
    if (clean.starts_with("Frequencies --")) {
      result.frequenciesPresent=true;
      const bool high=clean.starts_with("Frequencies ---");
      auto& frequencies=high?highPrecisionFrequencies:normalFrequencies;
      if(!high)sawNormalFrequencies=true;
      const auto offset=clean.find_first_not_of('-',clean.find("--"));
      if(offset!=std::string::npos)for(const auto& token:words(clean.substr(offset)))
        if(const auto frequency=parseNumber(token))frequencies.push_back(*frequency);
    }
    if (contains(low,"excited state")) { const auto stateWords=words(line); int index=0; for(size_t i=0;i+1<stateWords.size();++i) if(lower(stateWords[i])=="state") { std::string token=stateWords[i+1]; while(!token.empty()&&!std::isdigit(static_cast<unsigned char>(token.back()))) token.pop_back(); if(const auto parsed=parseNumber(token)) index=static_cast<int>(*parsed); break; } const auto ev=numberBefore(line,"eV"); if(index>0&&ev) result.excitedStates.push_back({index,*ev,numberBefore(line,"nm"),numberAfter(line,"f="),clean}); }
    if (contains(low,"point number")) { const auto pointWords=words(line); for(size_t i=0;i+1<pointWords.size();++i) if(lower(pointWords[i])=="number") { if(const auto parsed=parseNumber(pointWords[i+1])) { activeIrcIndex=static_cast<int>(*parsed); activeIrcDirection=contains(low,"forward")?"forward":contains(low,"reverse")?"reverse":""; break; } } }
    if (contains(low, "standard orientation:") || contains(low, "input orientation:")) { inOrientation = true; orientationDashes = 0; geometry.clear(); continue; }
    if (inOrientation) { if (dashed(line)) { ++orientationDashes; if (orientationDashes >= 3) { if (!geometry.empty()) lastGeometry = geometry; inOrientation = false; } continue; } if (orientationDashes >= 2) { const auto item = words(line); if (item.size() >= 6 && std::all_of(item[0].begin(), item[0].end(), ::isdigit)) { const int atomic = std::stoi(item[1]); const auto x = parseNumber(item[3]); const auto y = parseNumber(item[4]); const auto z = parseNumber(item[5]); if (atomic > 0 && x && y && z) geometry.push_back({atomic, elementSymbol(atomic), {*x, *y, *z}}); } } }
    if (contains(low, "mulliken charges and spin densities")) {
      if(inSpin)finishSpinBlock();
      inSpin=true;hydrogenSummedSpin=contains(low,"hydrogens summed");spins.clear();continue;
    }
    if (inSpin) {
      const auto item = words(line);
      const bool atomRow = item.size() == 4 &&
        std::all_of(item[0].begin(), item[0].end(), [](unsigned char c){ return std::isdigit(c); }) &&
        !item[1].empty() && std::isalpha(static_cast<unsigned char>(item[1][0]));
      const auto charge = atomRow ? parseNumber(item[2]) : std::nullopt;
      const auto spin = atomRow ? parseNumber(item[3]) : std::nullopt;
      if (charge && spin) {
        const int id = std::stoi(item[0]);
        if (id > 0 && (spins.empty() || id > spins.back().atomIndex))
          spins.push_back({id, item[1], *charge, *spin});
        else { finishSpinBlock(); inSpin = false; }
      } else if (!spins.empty() || (!clean.empty() &&
                 !((item.size()==1 && item[0]=="1") ||
                   (item.size()==2 && item[0]=="1" && item[1]=="2")))) {
        finishSpinBlock();
        inSpin = false;
      }
    }
  }
  if(collectingRoute)route=activeRoute;
  if(inSpin)finishSpinBlock();
  result.vibrationalFrequencies=sawNormalFrequencies?std::move(normalFrequencies):std::move(highPrecisionFrequencies);
  for(double frequency:result.vibrationalFrequencies)if(frequency<0)result.imaginaryFrequencies.push_back(frequency);
  result.route=route;
  result.finalGeometry=std::move(lastGeometry);
  result.spinDensities=fullSpins.empty()?std::move(summedSpins):std::move(fullSpins);
  // A restricted singlet has zero Mulliken spin at every atom even when
  // Gaussian omits the spin-population section altogether.
  if(result.spinDensities.empty()&&result.multiplicity==1&&restrictedScf&&!result.finalGeometry.empty())
    for(size_t index=0;index<result.finalGeometry.size();++index)
      result.spinDensities.push_back({static_cast<int>(index+1),result.finalGeometry[index].symbol,0.0,0.0});
  result.spinSourcePath=path;
  const auto fileUtf8=path.filename().u8string();
  const auto routeLower = lower(route), fileLower = lower(std::string(fileUtf8.begin(),fileUtf8.end()));
  result.basisLevel = contains(routeLower, "tzvpp") || contains(fileLower, "tzvpp") ? "TZVPP" : (contains(routeLower, "svp") || contains(fileLower, "svp") ? "SVP" : "unknown");
  result.spinBasisLevel = result.basisLevel;
  result.spinFallback = !result.spinDensities.empty() && result.basisLevel != "TZVPP";
  if (contains(routeLower, "irc")) result.jobType = "irc"; else if (contains(routeLower, "opt") && contains(routeLower, "freq")) result.jobType = "opt_freq"; else if (contains(routeLower, "freq")) result.jobType = "freq"; else if (contains(routeLower, "opt")) result.jobType = "opt"; else if (contains(routeLower, "scf") || result.scfEnergy) result.jobType = "single_point";
  for (const auto& type : {std::pair{"freq", contains(routeLower, "freq")}, {"opt", contains(routeLower, "opt")}, {"irc", contains(routeLower, "irc")}, {"scan", contains(routeLower, "scan") || contains(routeLower, "modredundant")}}) if (type.second) result.jobTypes.emplace_back(type.first);
  if (result.jobTypes.empty() && result.scfEnergy) result.jobTypes.emplace_back("single_point");
  const auto slash = route.find('/'); if (slash != std::string::npos) { const auto start = route.find_last_of(" \t", slash); result.method = trim(route.substr(start == std::string::npos ? 0 : start + 1, slash - (start == std::string::npos ? 0 : start + 1))); const auto end = route.find_first_of(" \t", slash + 1); result.basis = route.substr(slash + 1, end == std::string::npos ? std::string::npos : end - slash - 1); }
  if (!result.scfEnergy && !result.thermalGibbsCorrection && !result.gibbsFreeEnergy && result.route.empty()) { result.success = false; result.reason = "not recognized as a Gaussian output"; }
  if (!result.normalTermination) result.warnings.emplace_back("Gaussian normal termination not found");
  if (!result.scfEnergy) result.warnings.emplace_back("missing: SCF Done energy");
  // Only paired spin data may replace primary data. Geometry and thermochemistry stay primary.
  if(const auto candidate=includePairedSpin?pairedSinglePointPath(path):std::filesystem::path{};!candidate.empty()){
    const auto highBasis=parse(candidate);
    if(const auto match=validateSpe(result,highBasis);highBasis.success&&!highBasis.spinDensities.empty()&&(compatibleSpe(match)||match==SpeMatch::MissingData)){
      result.spinDensities=highBasis.spinDensities;result.spinSourcePath=candidate;
      result.spinBasisLevel=highBasis.basisLevel;result.spinFallback=false;
    }
  }
  return result;
}

std::shared_ptr<const GaussianResult> GaussianCache::get(const std::filesystem::path& source) {
  const auto path = std::filesystem::absolute(source); const auto size = std::filesystem::file_size(path); const auto modified = std::filesystem::last_write_time(path);
  const auto paired=includePairedSpin_?pairedSinglePointPath(path):std::filesystem::path{};
  const auto pairedSize=paired.empty()?0:std::filesystem::file_size(paired);
  const auto pairedModified=paired.empty()?std::filesystem::file_time_type{}:std::filesystem::last_write_time(paired);
  { std::lock_guard lock(mutex_);
  if (const auto it = entries_.find(path); it != entries_.end() && it->second.size == size && it->second.modified == modified &&
      it->second.pairedPath==paired && it->second.pairedSize==pairedSize && it->second.pairedModified==pairedModified) return it->second.result;
  }
  auto result = std::make_shared<GaussianResult>(parser_.parse(path,false));
  if(!paired.empty()){
    const auto highBasis=get(paired);
    if(const auto match=validateSpe(*result,*highBasis);highBasis->success&&!highBasis->spinDensities.empty()&&(compatibleSpe(match)||match==SpeMatch::MissingData)){
      result->spinDensities=highBasis->spinDensities;result->spinSourcePath=paired;
      result->spinBasisLevel=highBasis->basisLevel;result->spinFallback=false;
    }
  }
  { std::lock_guard lock(mutex_); entries_[path] = {size, modified, result, paired, pairedSize, pairedModified}; }
  return result;
}
void GaussianCache::clear() { std::lock_guard lock(mutex_); entries_.clear(); }
} // namespace si
