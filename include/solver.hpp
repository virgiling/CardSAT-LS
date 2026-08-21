#pragma once

#ifndef _solver_hpp_INCLUDED
#define _solver_hpp_INCLUDED

#include "cardsatLS.hpp"
#include "ccdcl.hpp"
#include "option.hpp"
#include "preprocess.hpp"
#include <memory>

class Solver {

public:
  std::unique_ptr<Preprocessor> m_preprocessor;
  std::unique_ptr<LocalSearch::CardSATLS> m_local_search;
  std::unique_ptr<CCDCLSolver> m_ccdcl_solver;
  vec<int> m_model;

public:
  Solver() {};
  ~Solver() = default;
  int run();
  bool build_model();
  void get_model();
};

#endif // _solver_hpp_INCLUDED
