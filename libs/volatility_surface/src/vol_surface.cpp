#include "vol_surface.h"

#include <fstream>
#include <sstream>
#include <vector>

using namespace std;

void VolatilitySurface::loadFromCSV(const string& path) {
    ifstream file(path);

    string line;
    getline(file, line); // skip header row: "strike,expiry,vol"

    while (getline(file, line)) {
        vector<string> fields;
        stringstream ss(line);
        string token;
        while (getline(ss, token, ','))
            fields.push_back(token);

        if (fields.size() < 3) continue;

        const double strike = stod(fields[0]);
        const int    expiry = stoi(fields[1]);
        const double vol    = stod(fields[2]);

        // Two-level nested map: volatilities_[strike][expiry] = σ
        // e.g. volatilities_[150.0][30] = 0.25 means "25% annual vol for
        // a 150-strike option with 30 days to expiry."
        volatilities_[strike][expiry] = vol;
    }
}

double VolatilitySurface::getVol(double strike, int expiry) {
    if (volatilities_.empty())
        return 0.20; // fallback: 20% annual vol if no surface loaded

    // lower_bound returns an iterator to the first element >= strike.
    // If all stored strikes are below the requested value, end() is returned
    // and we step back to the last element (highest available strike).
    // This "snap to nearest" approach avoids crashes from out-of-range queries.
    auto it = volatilities_.lower_bound(strike);
    if (it == volatilities_.end()) --it;

    // Same logic on the expiry axis of the inner map.
    auto& expiryMap = it->second;
    auto  it2       = expiryMap.lower_bound(expiry);
    if (it2 == expiryMap.end()) --it2;

    return it2->second;
    // Note: this is nearest-neighbour, not bilinear interpolation.
    // A production vol surface would interpolate between surrounding nodes
    // to avoid discontinuous jumps when the expiry crosses a grid point.
}
