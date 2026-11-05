#include "si/SIModel.hpp"
#include <algorithm>
#include <cmath>

namespace si {
namespace {
using Vec = std::array<double, 3>;
Vec sub(const Vec& a, const Vec& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
double dot(const Vec& a, const Vec& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec cross(const Vec& a, const Vec& b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
double norm(const Vec& a) { return std::sqrt(dot(a,a)); }
// Same covalent radii as the molecular viewer, in angstroms. These define a
// conservative bond-distance heuristic; RMS motion alone is not a bond test.
double reactionRadius(int atomicNumber) {
  switch(atomicNumber){
  case 1:return 0.31;case 6:return 0.76;case 7:return 0.71;case 8:return 0.66;
  case 9:return 0.57;case 14:return 1.11;case 15:return 1.07;case 16:return 1.05;
  case 17:return 1.02;case 26:return 1.32;case 27:return 1.26;case 28:return 1.24;
  case 29:return 1.32;case 30:return 1.22;case 35:return 1.20;
  default:return 0.85;
  }
}
std::optional<Vec> atom(const std::vector<Atom>& geometry, int index) { if (index < 1 || static_cast<size_t>(index) > geometry.size()) return std::nullopt; return geometry[index - 1].xyz; }
std::vector<Vec> centered(const std::vector<Atom>& g){
    const auto n=g.size();
    Vec center{};for(const auto& a:g)for(int k=0;k<3;++k)center[k]+=a.xyz[k]/n;
    std::vector<Vec> points;points.reserve(n);for(const auto& a:g)points.push_back(sub(a.xyz,center));return points;
}

std::vector<Vec> rigidFit(const std::vector<Vec>& source,const std::vector<Vec>& target){
  const auto n=source.size();
  double s[3][3]{};
  for(size_t i=0;i<n;++i)for(int a=0;a<3;++a)for(int b=0;b<3;++b)s[a][b]+=source[i][a]*target[i][b];
  // Horn quaternion fit: largest eigenvector of a symmetric 4x4 matrix.
  double h[4][4]{{s[0][0]+s[1][1]+s[2][2],s[1][2]-s[2][1],s[2][0]-s[0][2],s[0][1]-s[1][0]},
    {s[1][2]-s[2][1],s[0][0]-s[1][1]-s[2][2],s[0][1]+s[1][0],s[0][2]+s[2][0]},
    {s[2][0]-s[0][2],s[0][1]+s[1][0],-s[0][0]+s[1][1]-s[2][2],s[1][2]+s[2][1]},
    {s[0][1]-s[1][0],s[0][2]+s[2][0],s[1][2]+s[2][1],-s[0][0]-s[1][1]+s[2][2]}};
  double v[4][4]{};for(int i=0;i<4;++i)v[i][i]=1;
  for(int iteration=0;iteration<80;++iteration){
    int p=0,q=1;for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)if(std::abs(h[a][b])>std::abs(h[p][q])){p=a;q=b;}
    if(std::abs(h[p][q])<1e-12)break;
    const double theta=0.5*std::atan2(2*h[p][q],h[q][q]-h[p][p]);
    const double c=std::cos(theta),t=std::sin(theta),pp=h[p][p],qq=h[q][q],pq=h[p][q];
    for(int k=0;k<4;++k)if(k!=p&&k!=q){const double kp=h[k][p],kq=h[k][q];h[k][p]=h[p][k]=c*kp-t*kq;h[k][q]=h[q][k]=t*kp+c*kq;}
    h[p][p]=c*c*pp-2*c*t*pq+t*t*qq;h[q][q]=t*t*pp+2*c*t*pq+c*c*qq;h[p][q]=h[q][p]=0;
    for(int k=0;k<4;++k){const double kp=v[k][p],kq=v[k][q];v[k][p]=c*kp-t*kq;v[k][q]=t*kp+c*kq;}
  }
  int best=0;for(int k=1;k<4;++k)if(h[k][k]>h[best][best])best=k;
  const double w=v[0][best];const Vec u{v[1][best],v[2][best],v[3][best]};

  auto aligned=source;
  for(size_t i=0;i<n;++i){
    const auto uv=cross(u,source[i]),uuv=cross(u,uv);
    for(int k=0;k<3;++k)aligned[i][k]+=2*(w*uv[k]+uuv[k]);
  }
  return aligned;
}
}
SpeMatch validateSpe(const GaussianResult& primary,const GaussianResult& spe){
  if(!primary.success||!spe.success||!spe.scfEnergy)return SpeMatch::MissingData;
  if(primary.charge&&spe.charge&&primary.charge!=spe.charge)return SpeMatch::Charge;
  if(primary.multiplicity&&spe.multiplicity&&primary.multiplicity!=spe.multiplicity)return SpeMatch::Multiplicity;
  const auto& a=primary.finalGeometry;
  const auto& b=spe.finalGeometry;
  if(a.empty()||b.empty())return SpeMatch::MissingData;
  if(a.size()!=b.size())return SpeMatch::Atoms;
  for(size_t i=0;i<a.size();++i)
    if(a[i].atomicNumber!=b[i].atomicNumber)return SpeMatch::Atoms;
  const auto target=centered(a);
  const auto aligned=rigidFit(centered(b),target);
  double sum=0;
  for(size_t i=0;i<a.size();++i){
    const double deviation=norm(sub(aligned[i],target[i]));
    if(!std::isfinite(deviation)||deviation>0.05)return SpeMatch::Geometry;
    sum+=deviation*deviation;
  }
  if(std::sqrt(sum/a.size())>0.02)return SpeMatch::Geometry;
  return primary.charge&&spe.charge&&primary.multiplicity&&spe.multiplicity
    ?SpeMatch::Verified:SpeMatch::MissingConditions;
}
bool compatibleSpe(SpeMatch match){
  return match==SpeMatch::Verified||match==SpeMatch::MissingConditions;
}
std::optional<double> selectEnergy(EnergyRecord& value, EnergyBasis basis) {
  const auto& freq = value.frequency; const auto electronic = value.singlePoint && value.singlePoint->scfEnergy ? value.singlePoint->scfEnergy : freq.scfEnergy;
  if (basis == EnergyBasis::DefaultComposite || basis == EnergyBasis::Gibbs) {
    const bool hasSpe = value.singlePoint && value.singlePoint->scfEnergy;
    value.label = hasSpe ? "E_SPE + (G_freq - E_freq)" : "G_freq (SPE missing)";
    if (!freq.gibbsFreeEnergy || (hasSpe && !freq.scfEnergy))
      return value.selectedHartree = std::nullopt;
    return value.selectedHartree = hasSpe
      ? *value.singlePoint->scfEnergy + (*freq.gibbsFreeEnergy - *freq.scfEnergy)
      : *freq.gibbsFreeEnergy;
  }
  if (!electronic) return value.selectedHartree = std::nullopt;
  switch (basis) {
  case EnergyBasis::Electronic: value.label = "Electronic energy only"; return value.selectedHartree = electronic;
  case EnergyBasis::ElectronicZpe: value.label = "Electronic + ZPE"; return value.selectedHartree = freq.zeroPointCorrection ? *electronic + *freq.zeroPointCorrection : std::optional<double>{};
  case EnergyBasis::Enthalpy: value.label = value.singlePoint ? "TZVPP SPE + enthalpy correction" : "frequency enthalpy only"; return value.selectedHartree = freq.enthalpy ? (value.singlePoint && freq.scfEnergy ? *electronic + *freq.enthalpy - *freq.scfEnergy : *freq.enthalpy) : std::optional<double>{};
  case EnergyBasis::Gibbs:
  case EnergyBasis::DefaultComposite: break;
  case EnergyBasis::LegacyComposite: value.label = "TZVPP SPE + ZPE + thermal -T*S + dispersion"; if (!freq.zeroPointCorrection || !freq.thermalCorrection || !freq.entropy) return value.selectedHartree = std::optional<double>{}; return value.selectedHartree = *electronic + *freq.zeroPointCorrection + (*freq.thermalCorrection - *freq.zeroPointCorrection) - (thermochemistryTemperature(freq) * *freq.entropy / 1000.0) / HartreeToKcal + freq.dispersion.value_or(0.0);
  }
  return std::nullopt;
}
std::vector<RelativeEnergy> relativeEnergies(std::vector<EnergyRecord>& values, const EnergyRecord& reference, EnergyBasis basis) {
  auto referenceCopy = reference; const auto referenceEnergy = selectEnergy(referenceCopy, basis); std::vector<RelativeEnergy> output; output.reserve(values.size());
  for (auto& value : values) { const auto energy = selectEnergy(value, basis); const bool thermal=basis==EnergyBasis::DefaultComposite||basis==EnergyBasis::Gibbs||
      basis==EnergyBasis::Enthalpy||basis==EnergyBasis::LegacyComposite;
    const auto delta = energy && referenceEnergy && (!thermal||thermochemistryTemperature(value.frequency)==thermochemistryTemperature(reference.frequency)) ? std::optional<double>{*energy - *referenceEnergy} : std::optional<double>{}; output.push_back({value.name, delta, delta ? std::optional<double>{*delta * HartreeToKcal} : std::optional<double>{}}); } return output;
}
std::array<std::optional<double>,6> legacyAbsoluteEnergyValues(const EnergyRecord& value) {
  const auto& f=value.frequency;
  const auto kcal=[](const std::optional<double>& printed,const std::optional<double>& hartree)->std::optional<double>{
    if(printed)return std::round(*printed*1000.0)/1000.0;
    return hartree?std::optional<double>(std::round(*hartree*HartreeToKcal*1000.0)/1000.0):std::nullopt;
  };
  return {f.scfEnergy,value.singlePoint&&value.singlePoint->scfEnergy?value.singlePoint->scfEnergy:f.scfEnergy,
    kcal(f.zeroPointKcal,f.zeroPointCorrection),kcal(f.thermalTotalKcal,f.thermalCorrection),f.entropy,f.dispersion.value_or(0.0)};
}
std::vector<LegacyRelativeEnergy> legacyRelativeEnergies(const std::vector<EnergyRecord>& values, const EnergyRecord& reference, bool includeDispersion, bool includeComplexation) {
  const auto ref=legacyAbsoluteEnergyValues(reference);
  std::vector<LegacyRelativeEnergy> output;output.reserve(values.size());
  for(const auto& value:values){
    const auto abs=legacyAbsoluteEnergyValues(value);
    std::array<std::optional<double>,9> terms{};
    if(abs[0]&&ref[0])terms[0]=(*abs[0]-*ref[0])*HartreeToKcal;
    if(abs[1]&&ref[1])terms[2]=(*abs[1]-*ref[1])*HartreeToKcal;
    if(terms[0]&&terms[2])terms[1]=*terms[2]-*terms[0];
    if(abs[2]&&ref[2])terms[3]=*abs[2]-*ref[2];
    if(abs[3]&&ref[3]&&terms[3])terms[4]=*abs[3]-*ref[3]-*terms[3];
    const double temperature=thermochemistryTemperature(value.frequency);
    if(abs[4]&&ref[4]&&temperature==thermochemistryTemperature(reference.frequency))
      terms[5]=-(*abs[4]-*ref[4])*temperature/1000.0;
    terms[6]=includeDispersion?(*abs[5]-*ref[5])*HartreeToKcal:0.0;
    // The legacy Python table stores complexation as a per-row correction.
    terms[7]=includeComplexation?value.complexationKcal.value_or(0.0):0.0;
    if(terms[2]&&terms[3]&&terms[4]&&terms[5])
      terms[8]=*terms[2]+*terms[3]+*terms[4]+*terms[5]+*terms[6]+*terms[7];
    output.push_back({value.name,terms});
  }
  return output;
}
std::vector<int> relativeEnergyColumns(bool includeDispersion, bool includeComplexation) { std::vector<int> columns{0,1,2,3,4,5}; if(includeDispersion)columns.push_back(6);if(includeComplexation)columns.push_back(7);columns.push_back(8);return columns; }
std::optional<double> distance(const std::vector<Atom>& g, int a, int b) { const auto x=atom(g,a), y=atom(g,b); if (!x || !y) return {}; return norm(sub(*x,*y)); }
std::optional<double> angle(const std::vector<Atom>& g, int a, int b, int c) { const auto x=atom(g,a), y=atom(g,b), z=atom(g,c); if (!x||!y||!z) return {}; const auto u=sub(*x,*y), v=sub(*z,*y); const auto d=norm(u)*norm(v); if (!d) return {}; return std::acos(std::clamp(dot(u,v)/d,-1.0,1.0))*180.0/std::acos(-1.0); }
std::optional<double> dihedral(const std::vector<Atom>& g,int a,int b,int c,int d) { const auto w=atom(g,a),x=atom(g,b),y=atom(g,c),z=atom(g,d); if(!w||!x||!y||!z) return {}; const auto b0=sub(*w,*x),b1=sub(*y,*x),b2=sub(*z,*y); const auto n1=cross(b0,b1),n2=cross(b1,b2); const auto l=norm(b1); if(!l||!norm(n1)||!norm(n2)) return {}; const auto m1=cross(n1,{b1[0]/l,b1[1]/l,b1[2]/l}); return std::atan2(dot(m1,n2),dot(n1,n2))*180.0/std::acos(-1.0); }
std::vector<SpinDensity> strongestSpinDensities(const GaussianResult& result, std::size_t count) { auto values=result.spinDensities; std::partial_sort(values.begin(), values.begin()+std::min(count,values.size()), values.end(), [](const auto& a,const auto& b){return std::abs(a.spin)>std::abs(b.spin);}); values.resize(std::min(count,values.size())); return values; }

GeometrySuggestion suggestGeometry(const std::vector<EnergyRecord>& records, std::size_t count) {
  GeometrySuggestion output;
  const auto valid=[](const std::vector<Atom>& g){
    return !g.empty()&&std::all_of(g.begin(),g.end(),[](const Atom& a){
      return std::all_of(a.xyz.begin(),a.xyz.end(),[](double x){return std::isfinite(x);});
    });
  };
  const auto first=std::find_if(records.begin(),records.end(),[&](const auto& r){return valid(r.frequency.finalGeometry);});
  if(first==records.end())return output;
  const auto& reference=first->frequency.finalGeometry;
  const auto n=reference.size();
  std::vector<const std::vector<Atom>*> geometries;
  for(const auto& record:records){
    const auto& g=record.frequency.finalGeometry;
    if(g.size()!=n||!valid(g))continue;
    bool compatible=true;
    for(size_t i=0;i<n;++i)if(g[i].symbol!=reference[i].symbol)compatible=false;
    if(compatible){geometries.push_back(&g);output.recordIndices.push_back(static_cast<size_t>(&record-records.data()));}
  }
  output.compatibleStructures=geometries.size();
  if(geometries.size()<2)return output;
  const auto target=centered(reference);
  std::vector<double> movement(n,0.0);
  for(const auto* geometry:geometries){
    const auto source=centered(*geometry);
    const auto aligned=rigidFit(source,target);
    double total=0;
    for(size_t i=0;i<n;++i){
      const auto delta=sub(aligned[i],target[i]);const double squared=dot(delta,delta);
      movement[i]+=squared;total+=squared;
    }
    output.rmsd.push_back(std::sqrt(total/n));
  }
  for(auto& value:movement)value=std::sqrt(value/(geometries.size()-1));
  struct Candidate{std::array<int,2> ids;bool bondChange;double score;};std::vector<Candidate> candidates;
  for(size_t a=0;a<n;++a)for(size_t b=a+1;b<n;++b){
    const auto hydrogen=[](const Atom& atom){return atom.atomicNumber==1||atom.symbol=="H"||atom.symbol=="D"||atom.symbol=="T";};
    if(hydrogen(reference[a])&&hydrogen(reference[b]))continue;
    double low=1e100,high=0;
    for(const auto* g:geometries){const double d=norm(sub((*g)[a].xyz,(*g)[b].xyz));low=std::min(low,d);high=std::max(high,d);}
    const double bondLimit=1.25*(reactionRadius(reference[a].atomicNumber)+reactionRadius(reference[b].atomicNumber));
    // Never report a pair that stays nonbonded. In particular a 6-7 A
    // separation is relevant only if it also reaches a 1-2 A bond distance.
    if(low<0.4||low>bondLimit||(high>=6.0&&low>2.0)||high-low<0.15||std::max(movement[a],movement[b])<0.03)continue;
    candidates.push_back({{static_cast<int>(a+1),static_cast<int>(b+1)},high>bondLimit,(high-low)/bondLimit*(movement[a]+movement[b])});
  }
  std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
    if(a.bondChange!=b.bondChange)return a.bondChange>b.bondChange;
    return a.score!=b.score?a.score>b.score:a.ids<b.ids;
  });
  for(size_t i=0;i<std::min(count,candidates.size());++i)output.distances.push_back(candidates[i].ids);
  return output;
}
} // namespace si
