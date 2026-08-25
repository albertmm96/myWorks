// utils/tile_math.h
#pragma once
#include <tuple>

namespace tilemath {

	// slippy map helpers
	std::tuple<int, int> lonLatToTile(double lat, double lon, int z);
	std::tuple<double, double, double, double> tileBBox(int x, int y, int z);
}