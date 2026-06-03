#pragma once
#include <map>
#include <string>
class VolatilitySurface {

public:
  void loadFromCSV(const std::string &path);
  double getVol(double strike, int expiry);

private:
  std::map<double, std::map<int, double>> volatilities_;
};
