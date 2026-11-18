#include "solver.hpp"
#include "cardsatLS.hpp"
#include "ccdcl.hpp"
#include "option.hpp"
#include "preprocess.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

Solver::Solver() = default;
Solver::~Solver() = default;

void Solver::run() {
  std::srand(solver_options.seeds);
  Preprocessor input;
  int result = 0;
  if (solver_options.preprocessing) {
    result = input.do_preprocess(solver_options.filename.c_str());
    if (!result)
      std::printf("c preprocess finished\n");
  } else {
    input.read_file(solver_options.filename.c_str());
  }
  if (!result)
    result = solve(input);
  if (result == 10 && solver_options.preprocessing && !recover_model(input))
    result = 0;

  if (result == 10)
    print_solution(input);
  else
    std::puts(result == 20 ? "s UNSATISFIABLE" : "s UNKNOWN");
}

int Solver::solve(Preprocessor &input) {
  if (solver_options.mode) {
    ccdcl = std::make_unique<CCDCLSolver>(
        solver_options.seeds, solver_options.init_mode,
        solver_options.enable_swap, solver_options.ls_max_flips,
        solver_options.ls_max_non_improve_flip, solver_options.ls_swapprob,
        solver_options.ls_backbone_threshold,
        solver_options.ls_swap_non_improve_flip);
    ccdcl->parse_from_preprocess(&input);
    return ccdcl->solve();
  }

  local_search = std::make_unique<LocalSearch::CardSATLS>();
  if (!local_search->parse_from_preprocess(input))
    return 20;
  local_search->set_init_method(solver_options.init_mode);
  local_search->enable_swap = solver_options.enable_swap;
  local_search->max_flips = solver_options.ls_max_flips;
  local_search->max_non_improve_flip = solver_options.ls_max_non_improve_flip;
  local_search->swap_non_improve_flip = solver_options.ls_swap_non_improve_flip;
  local_search->swapprob = solver_options.ls_swapprob;
  local_search->backbone_threshold = solver_options.ls_backbone_threshold;
  return local_search->solve();
}

int Solver::variable_value(int variable) const {
  const int value = ccdcl ? ccdcl->val(variable) : local_search->cur_soln[variable];
  return value > 0 ? 1 : -1;
}

bool Solver::recover_model(Preprocessor &input) const {
  for (int v = 1; v <= input.orivars; ++v) {
    const int mapped = input.mapto[v];
    if (mapped)
      input.mapval[v] = variable_value(std::abs(mapped)) * pnsign(mapped);
  }
  if (!input.get_complete_model())
    return false;
  for (int v = 1; v <= input.orivars; ++v) {
    if (std::abs(input.mapval[v]) != 1)
      return false;
  }
  return true;
}

void Solver::print_solution(const Preprocessor &input) const {
  std::puts("s SATISFIABLE");
  if (ccdcl) {
    std::printf("c Solver time = %.2f\n", ccdcl->opt_time);
  } else {
    std::printf("c Solver time = %.2f, tries = %d, step = %u, total step = %" PRId64 "\n",
                local_search->opt_time, local_search->tries, local_search->step,
                local_search->total_step);
  }
  std::printf("v ");
  for (int v = 1; v <= input.orivars; ++v) {
    const int value =
        solver_options.preprocessing ? input.mapval[v] : variable_value(v);
    std::printf("%d ", value * v);
  }
  std::puts("0");
}

void Solver::print_progress() const {
  std::puts("s UNKNOWN");
  if (ccdcl) {
    std::printf("c CCDCLSolver terminated, time = %.2f\n", ccdcl->opt_time);
  } else if (local_search) {
    std::printf("c Solver terminated, tries = %d, step = %u, total step = %" PRId64
                ", unsat  clauses count = %d\n",
                local_search->tries, local_search->step, local_search->total_step,
                local_search->unsat_count);
  } else {
    std::puts("c Solver terminated during input or preprocessing");
  }
}
