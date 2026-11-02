#include "si/GaussianParser.hpp"
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: si_parse FILE\n"; return 2; }
  const si::GaussianParser parser; const auto result = parser.parse(argv[1]);
  std::cout << (result.success ? "OK" : "FAIL") << "\nSCF=" << (result.scfEnergy ? std::to_string(*result.scfEnergy) : "") << "\ngeometry=" << result.finalGeometry.size() << "\nexcited_states=" << result.excitedStates.size() << "\nirc_points=" << result.ircPoints.size() << "\n";
  return result.success ? 0 : 1;
}
