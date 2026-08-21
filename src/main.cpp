#include "option.hpp"
#include "solver.hpp"
#include <csignal>
#include <cstdlib>

using namespace std;
using namespace LocalSearch;

Solver *solver = nullptr;

void signal_handler(int signum) {
  printf("s UNKNOWN\n");
  if (OPT(mode)) {
    printf("c CCDCLSolver terminated, time = %.2f\n",
           solver->m_ccdcl_solver->opt_time);
  } else {
    printf(
        "c Solver terminated, tries = %d, step = %d, total step = %d, unsat  "
        "clauses count = %d\n",
        solver->m_local_search->tries, solver->m_local_search->step,
        solver->m_local_search->total_step,
        solver->m_local_search->hard_unsat_nb);
  }
  exit(0);
}

int main(int argc, char *argv[]) {

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  INIT_ARGS(argc, argv);
  PRINT_ARGS();

  solver = new Solver();
  int res = solver->run();
  if (res == 10) {
    printf("s SATISFIABLE\n");
    if (OPT(mode)) {
      printf("c Solver time = %.2f\n", solver->m_ccdcl_solver->opt_time);
    } else {
      printf("c Solver time = %.2f, tries = %d, step = %d, total step = %d\n",
             solver->m_local_search->opt_time, solver->m_local_search->tries,
             solver->m_local_search->step, solver->m_local_search->total_step);
    }
    solver->get_model();
  } else if (res == 20) {
    printf("s UNSATISFIABLE\n");
  } else {
    printf("s UNKNOWN\n");
  }
  delete solver;
  return 0;
}
