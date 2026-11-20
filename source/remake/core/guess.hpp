#pragma once
// Automatic initial estimates (KIN_CD.CPP automatic_guess).

#include <span>
#include <vector>

#include "core/fit.hpp"
#include "core/models.hpp"

namespace kin {

// `data` must be sorted by x. Fixed parameters take their setting's value and
// are used when estimating the others. The result is clamped to the bounds.
std::vector<double> automatic_guess(ModelId model, std::span<const Point> data,
                                    const std::vector<ParamSetting>& settings);

}  // namespace kin
