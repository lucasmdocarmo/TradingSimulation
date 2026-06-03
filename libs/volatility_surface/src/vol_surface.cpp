#include "vol_surface.h"
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;
void VolatilitySurface::loadFromCSV(const string &path) {
  ifstream file(path);

  string line;
  getline(file, line);

  while (getline(file, line)) {
    vector<string> fields;
    stringstream ss(line);
    string token;
    while (getline(ss, token, ',')) {
      fields.push_back(token);
    }
    if (fields.size() < 3)
      continue;
    double strike = std::stod(fields[0]);
    int expiry = std::stoi(fields[1]);
    double vol = std::stod(fields[2]);
    volatilities_[strike][expiry] = vol;
  }
}

double VolatilitySurface::getVol(double strike, int expiry) {

  // Return default if no data loaded
  if (volatilities_.empty())
    return 0.20;

  // point at strike greater than my strike
  auto it = volatilities_.lower_bound(strike);

  // If all strike is lower than requested give me highest of whats left
  if (it == volatilities_.end())
    --it;
  auto &expiryMap = it->second;

  // Same for expiry
  auto it2 = expiryMap.lower_bound(expiry);
  if (it2 == expiryMap.end())
    --it2;

  return it2->second;
}
