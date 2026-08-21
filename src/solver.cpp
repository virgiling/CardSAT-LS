#include "solver.hpp"
#include "cardsatLS.hpp"
#include "ccdcl.hpp"
#include "option.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace std;
using namespace LocalSearch;

int Solver::run() {
  srand(OPT(seeds));
  int res = 0;
  if (OPT(preprocessing)) {
    m_preprocessor = std::make_unique<Preprocessor>();
    res = m_preprocessor->do_preprocess(OPT(filename).c_str(),
                                        OPT(mode) == 0);
    if (res)
      return res;
    printf("c preprocess finished\n");
  } else {
    m_preprocessor = std::make_unique<Preprocessor>(OPT(filename).c_str());
  }

  if (OPT(mode)) {
    m_ccdcl_solver = std::make_unique<CCDCLSolver>(
        OPT(seeds), OPT(init_mode), OPT(enable_swap), OPT(ls_max_flips),
        OPT(ls_max_non_improve_flip), OPT(ls_swapprob),
        OPT(ls_backbone_threshold), OPT(ls_swap_non_improve_flip));
    m_ccdcl_solver->parse_from_preprocess(m_preprocessor.get());
    res = m_ccdcl_solver->solve();
    if (res == 10 && !build_model())
      res = 0;
  } else {

    m_local_search = std::make_unique<CardSATLS>();
    m_local_search->parse_from_preprocess(m_preprocessor.get());

    m_local_search->set_init_method(OPT(init_mode));
    m_local_search->enable_swap = OPT(enable_swap) == 1;

    m_local_search->max_flips = OPT(ls_max_flips);
    m_local_search->max_non_improve_flip = OPT(ls_max_non_improve_flip);
    m_local_search->swap_non_improve_flip = OPT(ls_swap_non_improve_flip);
    m_local_search->swapprob = OPT(ls_swapprob);
    m_local_search->backbone_threshold = OPT(ls_backbone_threshold);

    res = m_local_search->solve();

    if (res == 10 && !build_model())
      res = 0;

    m_local_search->free_memory();
  }
  return res;
}

bool Solver::build_model() {
  m_model.clear();
  m_model.growTo(m_preprocessor->orivars + 1, 0);

  if (!OPT(preprocessing)) {
    for (int i = 1; i <= m_preprocessor->orivars; ++i) {
      int val = OPT(mode) ? (m_ccdcl_solver->val(i) > 0 ? 1 : -1)
                          : (m_local_search->cur_soln[i] > 0 ? 1 : -1);
      m_model[i] = val;
    }
    return true;
  }

  if (OPT(mode)) {
    for (int i = 1; i <= m_preprocessor->orivars; i++) {
      if (m_preprocessor->mapto[i]) {
        m_preprocessor->mapval[i] =
            (m_ccdcl_solver->val(abs(m_preprocessor->mapto[i])) > 0 ? 1 : -1) *
            (m_preprocessor->mapto[i] > 0 ? 1 : -1);
      }
    }
  } else {
    for (int i = 1; i <= m_preprocessor->orivars; i++) {
      if (m_preprocessor->mapto[i]) {
        m_preprocessor->mapval[i] =
            (m_local_search->cur_soln[abs(m_preprocessor->mapto[i])] > 0 ? 1
                                                                         : -1) *
            (m_preprocessor->mapto[i] > 0 ? 1 : -1);
      }
    }
  }
  if (!m_preprocessor->get_complete_model())
    return false;
  for (int i = 1; i <= m_preprocessor->orivars; ++i) {
    if (abs(m_preprocessor->mapval[i]) != 1)
      return false;
    m_model[i] = m_preprocessor->mapval[i];
  }
  return true;
}

void Solver::get_model() {
  cout << "v ";
  for (int i = 1; i <= m_preprocessor->orivars; i++) {
    cout << i * m_model[i] << " ";
  }
  cout << "0" << endl;
}
