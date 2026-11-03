#include "si/GaussianParser.hpp"
#include "si/DocumentModel.hpp"
#include "si/SIModel.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include "ProjectStore.hpp"
#include "GaussianSnapshot.hpp"
#include "si/XyzWriter.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
bool check(bool value, const char* message) { if (!value) std::cerr << "FAILED: " << message << '\n'; return value; }
}

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: si_regression FIXTURE_DIRECTORY\n"; return 2; }
  const auto fixtures=std::filesystem::u8path(argv[1]); const si::GaussianParser parser;
  const auto ts=parser.parse(fixtures / "ts1_svp_opt_freq.out");
  const auto geometryResult=parser.parse(fixtures / "native_geometry.log");
  const auto tdIrc=parser.parse(fixtures / "td_irc_native.log");
  const auto pairedSpin=parser.parse(fixtures / "spin-tz-freq.out");
  const auto fallbackSpin=parser.parse(fixtures / "spin-fallback-freq.out");
  const auto invalidSibling=parser.parse(fixtures / "spin-invalid-freq.out");
  bool ok=true;
  {
    si::EnergyRecord ref,rotated,changed;
    ref.frequency.finalGeometry={{6,"C",{0,0,0}},{6,"C",{1.4,0,0}},{8,"O",{0,1.3,0}},{1,"H",{0,0,1.0}}};
    rotated=ref;
    for(auto& a:rotated.frequency.finalGeometry){const auto p=a.xyz;a.xyz={8-p[1],-3+p[0],2+p[2]};}
    const auto rigid=si::suggestGeometry({ref,rotated});
    ok&=check(rigid.distances.empty()&&rigid.rmsd.size()==2&&rigid.rmsd[1]<1e-8,"RMSD removes rigid rotation and translation");
    changed=ref;changed.frequency.finalGeometry[3].xyz[2]+=0.7;
    const auto reaction=si::suggestGeometry({ref,changed},1);
    ok&=check(reaction.distances.size()==1&&reaction.distances[0]==std::array<int,2>{1,4}&&reaction.rmsd[1]>0.1,"automatic geometry detects changing reaction distance");
    ok&=check(si::suggestGeometry({ref}).distances.empty(),"automatic geometry requires comparison structures");
    changed.frequency.finalGeometry[0].symbol="N";
    ok&=check(si::suggestGeometry({ref,changed}).compatibleStructures==1,"automatic geometry excludes incompatible atom identities");
    rotated=ref;for(auto& a:rotated.frequency.finalGeometry)a.xyz[0]*=-1;
    const auto mirror=si::suggestGeometry({ref,rotated});
    ok&=check(mirror.rmsd.size()==2&&mirror.rmsd[1]>0.1,"RMSD does not align reflections as proper rotations");
    auto pairSuggestion=[](int z,const std::string& symbol,double firstDistance,double lastDistance){
      si::EnergyRecord a,b;a.frequency.finalGeometry={{z,symbol,{0,0,0}},{z,symbol,{firstDistance,0,0}}};
      b=a;b.frequency.finalGeometry[1].xyz[0]=lastDistance;return si::suggestGeometry({a,b});
    };
    ok&=check(pairSuggestion(1,"H",0.8,2.0).distances.empty(),"H-H pairs are always excluded from automatic geometry");
    ok&=check(pairSuggestion(6,"C",6.5,5.0).distances.empty(),"long-range motion without bonding is excluded");
    ok&=check(pairSuggestion(6,"C",6.5,2.5).distances.empty(),"long-range approach that stays nonbonded is excluded");
    ok&=check(pairSuggestion(6,"C",2.5,3.4).distances.empty(),"nearby nonbonded pair is excluded despite large motion");
    ok&=check(pairSuggestion(6,"C",6.5,1.5).distances.size()==1,"long-range approach to a bond distance is retained");
    ok&=check(pairSuggestion(6,"C",1.5,6.5).distances.size()==1,"bond dissociation is retained in either input order");
    ok&=check(pairSuggestion(6,"C",1.5,1.55).distances.empty(),"small spectator bond changes are excluded");
    ok&=check(pairSuggestion(16,"S",6.5,2.2).distances.empty(),"long-range pair must reach at most 2 A even for larger atoms");
    ok&=check(pairSuggestion(6,"C",1.5,1.7).distances.size()==1,"substantial bonded distance change is retained");
    ref.frequency.finalGeometry={{6,"C",{0,0,0}},{6,"C",{1.4,0,0}},{6,"C",{0,20,0}},{6,"C",{1.8,20,0}}};
    changed=ref;changed.frequency.finalGeometry[1].xyz[0]=1.8;changed.frequency.finalGeometry[3].xyz[0]=2.1;
    const auto prioritized=si::suggestGeometry({ref,changed},1);
    ok&=check(prioritized.distances.size()==1&&prioritized.distances[0]==std::array<int,2>{3,4},"bond formation or cleavage outranks spectator stretching");
  }
  const auto boundedSpin=parser.parse(fixtures / "spin-block-boundaries.out");
  ok &= check(boundedSpin.spinDensities.size()==3&&boundedSpin.spinDensities[0].atomIndex==1&&
    boundedSpin.spinDensities[1].atomIndex==2&&boundedSpin.spinDensities[2].atomIndex==3&&
    std::abs(boundedSpin.spinDensities[0].spin-1.2)<1e-9&&
    std::abs(boundedSpin.spinDensities[1].spin-0.4)<1e-9&&
    std::abs(boundedSpin.spinDensities[2].spin-0.4)<1e-9,
    "full atom-wise Mulliken populations take priority over hydrogen-summed values");
  const auto restricted=parser.parse(fixtures / "restricted_singlet.out");
  ok &= check(restricted.finalGeometry.size()==3&&restricted.spinDensities.size()==3&&
    std::all_of(restricted.spinDensities.begin(),restricted.spinDensities.end(),[](const auto& atom){return atom.spin==0.0;}),
    "restricted singlet has one zero Mulliken spin per geometry atom");
  ok &= check(ts.success, "TS fixture parses");
  ok &= check(ts.scfEnergy && std::abs(*ts.scfEnergy + 150.101010) < 1e-8, "TS SCF energy");
  ok &= check(ts.spinDensities.size()==2, "TS Mulliken spin density count");
  ok &= check(geometryResult.finalGeometry.size()==3, "final geometry");
  ok &= check(ts.gibbsFreeEnergy.has_value(), "TS Gibbs free energy");
  ok &= check(tdIrc.success && tdIrc.normalTermination, "TD/IRC fixture termination");
  ok &= check(tdIrc.excitedStates.size()==2, "TD excited-state count");
  ok &= check(tdIrc.excitedStates.size()>1 && std::abs(tdIrc.excitedStates[1].energyEv-4.5678)<1e-6, "TD energy extraction");
  ok &= check(tdIrc.ircPoints.size()==2, "IRC point count");
  ok &= check(tdIrc.ircPoints.size()>1 && tdIrc.ircPoints[0].direction=="forward" && tdIrc.ircPoints[1].direction=="reverse", "IRC directions");
  ok &= check(tdIrc.entropy && std::abs(*tdIrc.entropy-100.0)<1e-6 && tdIrc.dispersion && std::abs(*tdIrc.dispersion+0.01)<1e-6, "entropy and dispersion extraction");
  ok &= check(pairedSpin.spinBasisLevel=="TZVPP"&&!pairedSpin.spinFallback&&pairedSpin.spinDensities.size()==1&&std::abs(pairedSpin.spinDensities[0].spin-1.222)<1e-9,"TZVPP sibling spin source");
  ok &= check(fallbackSpin.spinBasisLevel=="SVP"&&fallbackSpin.spinFallback&&std::abs(fallbackSpin.spinDensities[0].spin-0.333)<1e-9,"SVP spin fallback");
  ok &= check(!invalidSibling.spinFallback && invalidSibling.spinSourcePath.filename()=="spin-invalid-freq-bb.out","paired SPE spin is preferred independent of basis name");
  si::EnergyRecord composite{"td-irc",tdIrc,{}}; const auto compositeEnergy=si::selectEnergy(composite,si::EnergyBasis::LegacyComposite);
  ok &= check(compositeEnergy.has_value(), "default composite energy selection");
  const std::vector<si::Atom> geometry{{1,"H",{0.0,0.0,0.0}},{1,"H",{1.0,0.0,0.0}},{1,"H",{1.0,1.0,0.0}},{1,"H",{1.0,1.0,1.0}}};
  ok &= check(si::distance(geometry,1,2) && std::abs(*si::distance(geometry,1,2)-1.0)<1e-9,"distance calculation");
  ok &= check(si::angle(geometry,1,2,3) && std::abs(*si::angle(geometry,1,2,3)-90.0)<1e-9,"angle calculation");
  ok &= check(si::dihedral(geometry,1,2,3,4) && std::abs(std::abs(*si::dihedral(geometry,1,2,3,4))-90.0)<1e-9,"dihedral calculation");
  si::SIDocumentModel document;document.title="Supporting Information";document.tables.append({si::DocumentSection::AbsoluteEnergy,"Absolute energy",{"Structure","E"},{{"TS","-150.101010"}},{}});document.coordinates.append({"TS",ts.finalGeometry});
  ok &= check(document.tableCount()==1 && document.rowCount(si::DocumentSection::AbsoluteEnergy)==1,"document model table and row count");
  ok &= check(document.coordinates.size()==1 && document.coordinates.front().structureName=="TS","document model structure name");
  ok &= check(document.coordinates.front().atoms.size()==ts.finalGeometry.size(),"document model coordinates");
  ok &= check(si::formatNumber(1.23456,3,4)==QStringLiteral("1.2346")&&si::formatNumber(1.23456,3,0)==QStringLiteral("1.235"),"decimal precision override");
  si::EnergyRecord reference{"ref",{},{}},sample{"sample",{}, {}};reference.frequency.scfEnergy=-100.0;reference.frequency.zeroPointCorrection=0.10;reference.frequency.thermalCorrection=0.12;reference.frequency.entropy=50.0;reference.frequency.dispersion=-0.01;sample.frequency.scfEnergy=-99.9;sample.frequency.zeroPointCorrection=0.11;sample.frequency.thermalCorrection=0.14;sample.frequency.entropy=55.0;sample.frequency.dispersion=-0.005;
  for(const auto [dispersion,complexation]:{std::pair{false,false},std::pair{false,true},std::pair{true,false},std::pair{true,true}}){const auto values=si::legacyRelativeEnergies({sample},reference,dispersion,complexation);ok&=check(values.size()==1&&values[0].kcalTerms[8].has_value(),"relative energy option combination");const auto columns=si::relativeEnergyColumns(dispersion,complexation);ok&=check(columns.size()==static_cast<size_t>(7+(dispersion?1:0)+(complexation?1:0)),"relative table column combination");}
  sample.frequency.dispersion.reset();const auto withoutDispersion=si::legacyRelativeEnergies({sample},reference,false,true);ok&=check(withoutDispersion[0].kcalTerms[8].has_value(),"missing dispersion allowed when excluded");

  QTemporaryDir temporary;
  const auto metadataPath=temporary.filePath("3TS-freq.out");
  QFile metadataFile(metadataPath);if(!metadataFile.open(QIODevice::WriteOnly))return 1;
  metadataFile.write(
    " Gaussian 16: EM64L-G16RevC.01 3-Jul-2019\n"
    " # opt freq UB3LYP/Def2-SVP scrf=(smd,solvent=water) empiricaldispersion=gd3bj\n\n"
    " Charge = 0 Multiplicity = 3\n"
    " SCF Done: E(UB3LYP) = -100.00000000 A.U.\n"
    " Temperature   298.150 Kelvin.  Pressure 1.00000 Atm.\n"
    " S**2 before annihilation     2.0123, after     2.0001\n"
    " Frequencies -- -123.45 100.0 200.0\n"
    " Thermal correction to Gibbs Free Energy= 0.02000000\n"
    " Sum of electronic and thermal Free Energies= -99.98000000\n"
    " R6Disp = -0.123\n"
    " Normal termination of Gaussian 16\n");
  metadataFile.close();
  const auto metadata=parser.parse(std::filesystem::path(metadataPath.toStdWString()));
  ok&=check(metadata.temperatureKelvin&&std::abs(*metadata.temperatureKelvin-298.15)<1e-9,"temperature parsing");
  ok&=check(metadata.pressureAtm&&std::abs(*metadata.pressureAtm-1.0)<1e-9,"pressure parsing");
  ok&=check(metadata.sSquaredBefore&&std::abs(*metadata.sSquaredBefore-2.0123)<1e-9&&metadata.sSquaredAfter&&std::abs(*metadata.sSquaredAfter-2.0001)<1e-9,"S squared before/after");
  ok&=check(metadata.program=="Gaussian"&&metadata.version=="16"&&metadata.revision=="C.01","program revision parsing");
  ok&=check(metadata.frequenciesPresent&&metadata.imaginaryFrequencies.size()==1,"imaginary frequency count retained");
  if(!metadataFile.open(QIODevice::WriteOnly|QIODevice::Truncate))return 1;
  metadataFile.write(" Gaussian 16, Revision C.01\n # freq B3LYP/6-31G\n\n Temperature ***** Kelvin.\n S**2 before annihilation ****, after ****\n SCF Done: E = -1.0\n");metadataFile.close();
  const auto incompleteMetadata=parser.parse(std::filesystem::path(metadataPath.toStdWString()));
  ok&=check(incompleteMetadata.revision=="C.01"&&!incompleteMetadata.temperatureKelvin&&!incompleteMetadata.sSquaredBefore&&!incompleteMetadata.sSquaredAfter,"unknown metadata remains absent");

  const auto cachePrimary=temporary.filePath("unicode-한글-freq.out");
  const auto cachePaired=temporary.filePath("unicode-한글-freq-bb.out");
  QFile primary(cachePrimary);if(!primary.open(QIODevice::WriteOnly))return 1;primary.write("# freq B3LYP/6-31G\n\n SCF Done: E = -1.0\n");primary.close();
  QFile paired(cachePaired);if(!paired.open(QIODevice::WriteOnly))return 1;paired.write("# SP B3LYP/6-311G\n\n SCF Done: E = -1.1\n Mulliken charges and spin densities:\n 1 H 0.0 0.5\n\n");paired.close();
  si::GaussianCache cache;
  const auto cached=cache.get(std::filesystem::path(cachePrimary.toStdWString()));
  ok&=check(cache.get(std::filesystem::path(cachePrimary.toStdWString()))==cached&&cached->spinDensities.front().spin==0.5,"Unicode pairing and unchanged cache reuse");
  if(!paired.open(QIODevice::WriteOnly|QIODevice::Truncate))return 1;paired.write("# SP B3LYP/6-311G\n\n SCF Done: E = -1.1\n Mulliken charges and spin densities:\n 1 H 0.0 0.7500\n\n");paired.close();
  const auto recached=cache.get(std::filesystem::path(cachePrimary.toStdWString()));
  ok&=check(recached!=cached&&recached->spinDensities.front().spin==0.75,"paired-file changes invalidate primary spin cache");
  si::EnergyRecord corrected{"A",metadata,si::GaussianResult{}};
  corrected.singlePoint->scfEnergy=-101.0;
  const auto selected=si::selectEnergy(corrected,si::EnergyBasis::DefaultComposite);
  ok&=check(selected&&std::abs(*selected-(-101.0+(-99.98-(-100.0))))<1e-10,"new composite exact formula; no dispersion added");
  corrected.frequency.dispersion=-99.0;
  ok&=check(si::selectEnergy(corrected,si::EnergyBasis::DefaultComposite)==selected,"dispersion is metadata only");
  corrected.singlePoint.reset();
  ok&=check(si::selectEnergy(corrected,si::EnergyBasis::DefaultComposite)==metadata.gibbsFreeEnergy&&corrected.label.find("SPE missing")!=std::string::npos,"missing SPE fallback");
  corrected.frequency.gibbsFreeEnergy.reset();
  ok&=check(!si::selectEnergy(corrected,si::EnergyBasis::DefaultComposite),"missing Gibbs is not zero");
  ok&=check(si::formatNumber(std::optional<double>{},8,0)=="N/A"&&si::formatNumber(-100.0,8,0)=="-100.00000000","Hartree precision and missing data");
  const auto noComplexation=si::legacyRelativeEnergies({sample},reference,false,true);
  ok&=check(noComplexation[0].kcalTerms[7]==0.0,"legacy table uses zero when no complexation correction is supplied");
  sample.complexationKcal=2.0;reference.complexationKcal=0.5;
  const auto withComplexation=si::legacyRelativeEnergies({sample},reference,false,true);
  const auto excludedComplexation=si::legacyRelativeEnergies({sample},reference,false,false);
  ok&=check(withComplexation[0].kcalTerms[7]&&std::abs(*withComplexation[0].kcalTerms[8]-*excludedComplexation[0].kcalTerms[8]-2.0)<1e-9,"legacy Python per-row complexation correction");

  si::EnergyRecord draftRecord{"3TS",metadata,{}};
  QStringList conditionWarnings;
  const auto draft=si::computationalDetails({draftRecord},si::EnergyBasis::DefaultComposite,&conditionWarnings);
  ok&=check(draft.contains("Gaussian 16 Revision C.01")&&draft.contains("298.15 K")&&draft.contains("UB3LYP/Def2-SVP")&&draft.contains("scrf")&&draft.contains("E_SPE"),"deterministic computational details");
  auto different=draftRecord;different.frequency.method="PBE0";different.frequency.temperatureKelvin=310.0;
  conditionWarnings.clear();const auto mixed=si::computationalDetails({draftRecord,different},si::EnergyBasis::DefaultComposite,&conditionWarnings);
  ok&=check(conditionWarnings.size()>=2&&!mixed.contains("at the UB3LYP/Def2-SVP level")&&!mixed.contains("at 298.15 K"),"mixed conditions do not produce uniform claims");
  const auto validation=si::validationTable({draftRecord});
  ok&=check(validation.rows.front()[4]=="1"&&validation.rows.front()[5]=="-123.45"&&validation.rows.front()[6]=="2.012","validation table");

  si::SIDocumentModel xyzModel;
  auto preciseGeometry=geometry;preciseGeometry[0].xyz[0]=1.23456789012345;
  xyzModel.coordinates.append({"A",preciseGeometry,0,1,-101.0});xyzModel.coordinates.append({"B",geometry,-1,3,{}});
  QString error;const auto xyzPath=temporary.filePath("structures.xyz");
  ok&=check(si::XyzWriter::write(xyzPath,xyzModel,&error),"XYZ write");
  QFile xyz(xyzPath);if(!xyz.open(QIODevice::ReadOnly))return 1;const auto xyzText=QString::fromUtf8(xyz.readAll().replace("\r\n","\n"));
  const auto xyzLines=xyzText.split('\n',Qt::SkipEmptyParts);
  int offset=0,frames=0;bool completeFrames=true;
  while(offset<xyzLines.size()){
    bool numeric=false;const int atomCount=xyzLines[offset].toInt(&numeric);
    if(!numeric||atomCount!=4||offset+atomCount+2>xyzLines.size()){completeFrames=false;break;}
    for(int index=0;index<atomCount;++index)
      completeFrames&=xyzLines[offset+2+index].simplified().split(' ').size()==4;
    offset+=atomCount+2;++frames;
  }
  ok&=check(completeFrames&&frames==2&&offset==xyzLines.size()&&
    xyzText.startsWith("4\nA | charge=0 | multiplicity=1 | E_SPE=-101.00000000 Eh\nH")&&
    xyzText.contains("1.23456789012345")&&xyzText.contains("4\nB | charge=-1 | multiplicity=3 | E_SPE=N/A Eh"),
    "all XYZ frames contain the full atom list and lossless coordinates");

  // The extracted Gaussian data must survive when the original output is gone.
  const auto snapshot=si::GaussianSnapshot::encode(metadata);
  si::GaussianResult unpacked;
  ok&=check(si::GaussianSnapshot::decode(snapshot,&unpacked) &&
    unpacked.scfEnergy==metadata.scfEnergy &&
    unpacked.gibbsFreeEnergy==metadata.gibbsFreeEnergy &&
    unpacked.temperatureKelvin==metadata.temperatureKelvin &&
    unpacked.imaginaryFrequencies==metadata.imaginaryFrequencies &&
    unpacked.program==metadata.program &&
    unpacked.spinSourcePath==metadata.spinSourcePath,
    "Gaussian thermochemistry and metadata snapshot roundtrip");
  const auto geometrySnapshot=si::GaussianSnapshot::encode(geometryResult);
  ok&=check(si::GaussianSnapshot::decode(geometrySnapshot,&unpacked) &&
    unpacked.finalGeometry.size()==geometryResult.finalGeometry.size() &&
    unpacked.finalGeometry.front().xyz==geometryResult.finalGeometry.front().xyz &&
    unpacked.spinDensities.size()==geometryResult.spinDensities.size(),
    "Gaussian final coordinates and spin snapshot roundtrip");
  const auto excitedSnapshot=si::GaussianSnapshot::encode(tdIrc);
  ok&=check(si::GaussianSnapshot::decode(excitedSnapshot,&unpacked) &&
    unpacked.ircPoints.size()==tdIrc.ircPoints.size() &&
    unpacked.excitedStates.size()==tdIrc.excitedStates.size(),
    "Gaussian TD-DFT and IRC snapshot roundtrip");
  auto corrupted=snapshot;corrupted["sha256"]="invalid";
  ok&=check(!si::GaussianSnapshot::decode(corrupted,&unpacked),
    "corrupted embedded Gaussian snapshot is rejected");
    ProjectState project;project.manuscriptTitle="A manuscript";project.authors="A. Author";project.affiliations="Laboratory";project.correspondingEmail="author@example.org";project.computationalDetails="User edited text";project.computationalDetailsEdited=true;project.referencesText="Reference 1";
  project.files={metadataPath};
  project.gaussianSnapshots.insert(metadataPath,snapshot);
  project.pairedSnapshotPaths.insert(metadataPath,cachePaired);
  project.gaussianSnapshots.insert(cachePaired,si::GaussianSnapshot::encode(*recached));
  const auto projectPath=temporary.filePath("project.json");
  ok&=check(ProjectStore::save(projectPath,project,&error),"save v4 project");
  ProjectState restored;ok&=check(ProjectStore::load(projectPath,&restored,&error)&&restored.energyBasis=="0"&&restored.manuscriptTitle==project.manuscriptTitle&&restored.computationalDetails==project.computationalDetails&&restored.computationalDetailsEdited&&restored.referencesText==project.referencesText,"v4 metadata and new basis roundtrip");
  QFile::remove(metadataPath);
  ok&=check(restored.gaussianSnapshots.contains(metadataPath) &&
    restored.pairedSnapshotPaths.value(metadataPath).toString()==cachePaired &&
    si::GaussianSnapshot::decode(restored.gaussianSnapshots.value(metadataPath).toObject(),&unpacked) &&
    unpacked.scfEnergy==metadata.scfEnergy &&
    !QFile::exists(metadataPath),"project retains data after original output deletion");
  for(int oldBasis=0;oldBasis<=4;++oldBasis){
    QFile legacy(projectPath);if(!legacy.open(QIODevice::WriteOnly|QIODevice::Truncate))return 1;
    legacy.write(QJsonDocument(QJsonObject{{"version",3},{"reference_settings",QJsonObject{{"energy_basis",QString::number(oldBasis)}}}}).toJson());legacy.close();
    ok&=check(ProjectStore::load(projectPath,&restored,&error)&&restored.energyBasis==QString::number(oldBasis==0?5:oldBasis),"v3 energy basis compatibility");
  }
  std::cout << (ok ? "native Gaussian regression: PASS\n" : "native Gaussian regression: FAIL\n");
  return ok ? 0 : 1;
}
