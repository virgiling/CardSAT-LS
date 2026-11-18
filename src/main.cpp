#include "option.hpp"
#include "solver.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {
Solver *active_solver = nullptr;

void signal_handler(int) {
  if (active_solver) {
    active_solver->print_progress();
  } else {
    std::puts("s UNKNOWN\nc Solver terminated during input or preprocessing");
  }
  std::exit(0);
}
} // namespace

int main(int argc, char *argv[]) {
  Solver solver;
  active_solver = &solver;
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGINT, signal_handler);

  solver_options.parse_args(argc, argv);
  if (solver_options.verbose)
    solver_options.print_change();
  solver.run();
  active_solver = nullptr;
}
