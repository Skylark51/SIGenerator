#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace si {

struct Atom { int atomicNumber{}; std::string symbol; std::array<double, 3> xyz{}; };
struct SpinDensity { int atomIndex{}; std::string atom; double charge{}; double spin{}; };
struct ExcitedState { int index{}; double energyEv{}; std::optional<double> wavelengthNm; std::optional<double> oscillatorStrength; std::string description; };
struct IrcPoint { int index{}; std::string direction; std::optional<double> scfEnergy; };
struct GaussianResult {
  std::filesystem::path path;
  bool success{true};
  std::string reason;
  std::vector<std::string> warnings;
  std::optional<double> scfEnergy, zeroPointCorrection, thermalCorrection, thermalGibbsCorrection, enthalpy, gibbsFreeEnergy, entropy, dispersion;
  std::optional<double> zeroPointKcal, thermalTotalKcal;
  std::optional<std::string> program, version, revision;
  std::optional<double> temperatureKelvin, pressureAtm, sSquaredBefore, sSquaredAfter;
  bool frequenciesPresent{};
  std::vector<double> imaginaryFrequencies;
  std::vector<SpinDensity> spinDensities;
  std::vector<ExcitedState> excitedStates;
  std::vector<IrcPoint> ircPoints;
  std::filesystem::path spinSourcePath;
  std::string spinBasisLevel{"unknown"};
  bool spinFallback{};
  std::optional<int> charge, multiplicity;
  bool normalTermination{};
  std::string route, method, basis, jobType{"unknown"}, basisLevel{"unknown"};
  std::vector<std::string> jobTypes;
  std::vector<Atom> finalGeometry;
};

[[nodiscard]] std::filesystem::path pairedSinglePointPath(const std::filesystem::path& path);

class GaussianParser {
public:
  [[nodiscard]] GaussianResult parse(const std::filesystem::path& path, bool includePairedSpin = true) const;
  [[nodiscard]] static std::string elementSymbol(int atomicNumber);
};

class GaussianCache {
public:
  [[nodiscard]] std::shared_ptr<const GaussianResult> get(const std::filesystem::path& path);
  void clear();
private:
  struct Entry { std::uintmax_t size{}; std::filesystem::file_time_type modified{}; std::shared_ptr<GaussianResult> result; std::filesystem::path pairedPath; std::uintmax_t pairedSize{}; std::filesystem::file_time_type pairedModified{}; };
  GaussianParser parser_;
  std::mutex mutex_;
  std::unordered_map<std::filesystem::path, Entry> entries_;
};

} // namespace si
