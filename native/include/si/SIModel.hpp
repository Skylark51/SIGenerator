#pragma once

#include "si/GaussianParser.hpp"
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace si {
inline constexpr double HartreeToKcal = 627.509474;
enum class EnergyBasis { DefaultComposite, Electronic, ElectronicZpe, Enthalpy, Gibbs, LegacyComposite };
struct EnergyRecord { std::string name; GaussianResult frequency; std::optional<GaussianResult> singlePoint; std::optional<double> selectedHartree; std::string label; std::optional<double> complexationKcal; };
struct RelativeEnergy { std::string name; std::optional<double> hartree; std::optional<double> kcalPerMol; };
struct LegacyRelativeEnergy { std::string name; std::array<std::optional<double>, 9> kcalTerms; };
[[nodiscard]] std::optional<double> selectEnergy(EnergyRecord& value, EnergyBasis basis);
[[nodiscard]] std::vector<RelativeEnergy> relativeEnergies(std::vector<EnergyRecord>& values, const EnergyRecord& reference, EnergyBasis basis);
[[nodiscard]] std::vector<LegacyRelativeEnergy> legacyRelativeEnergies(const std::vector<EnergyRecord>& values, const EnergyRecord& reference, bool includeDispersion=true, bool includeComplexation=true);
[[nodiscard]] std::array<std::optional<double>,6> legacyAbsoluteEnergyValues(const EnergyRecord& value);
[[nodiscard]] std::vector<int> relativeEnergyColumns(bool includeDispersion=true, bool includeComplexation=true);
[[nodiscard]] std::optional<double> distance(const std::vector<Atom>& geometry, int first, int second);
[[nodiscard]] std::optional<double> angle(const std::vector<Atom>& geometry, int first, int middle, int last);
[[nodiscard]] std::optional<double> dihedral(const std::vector<Atom>& geometry, int first, int second, int third, int fourth);
[[nodiscard]] std::vector<SpinDensity> strongestSpinDensities(const GaussianResult& result, std::size_t count);
struct GeometrySuggestion {
  std::vector<std::array<int,2>> distances;
  std::vector<double> rmsd;
  std::vector<std::size_t> recordIndices;
  std::size_t compatibleStructures{};
};
// Atom IDs must correspond across structures. Rigid rotation and translation
// are removed before accumulating per-atom RMS displacement.
[[nodiscard]] GeometrySuggestion suggestGeometry(const std::vector<EnergyRecord>& records, std::size_t count=6);
} // namespace si
