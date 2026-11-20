#pragma once
// Shared by the two files of the 'cd' group: the result and progress
// screens (results.cpp, from KIN_CD.CPP print_*_results and the solver
// loops of MATHF.CPP) used by the fitting driver (kin_cd.cpp).

#include "classic/kinetics.hpp"
#include "core/analysis.hpp"

namespace classic::cd {

// The result screen of a finished fit, in the layout of its method:
// print_linear_results, print_mrq_results or print_gn_results. `aborted`
// gives the Gauss-Newton screen v3.51's "Run aborted by user !!" message.
void draw_final_results(const kin::Analysis& a, bool aborted);

// Draws the result screen once for each iteration in a.fit.history, a
// frame apart, as v3.51's solvers did while they ran. With `abortable`,
// returns the iteration at which the user pressed ESC (v3.51 stopped the
// fit there), or 0. Any other key (or ESC, when not abortable) ends the
// display early and is left in the keyboard buffer.
Task<int> show_progress(const kin::Analysis& a, bool abortable);

}  // namespace classic::cd
