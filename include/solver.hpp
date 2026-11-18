#pragma once

#include <memory>

class CCDCLSolver;
class Preprocessor;
namespace LocalSearch {
class CardSATLS;
}

class Solver {
public:
  Solver();
  ~Solver();

  void run();
  void print_progress() const;

private:
  std::unique_ptr<LocalSearch::CardSATLS> local_search;
  std::unique_ptr<CCDCLSolver> ccdcl;

  int solve(Preprocessor &input);
  int variable_value(int variable) const;
  bool recover_model(Preprocessor &input) const;
  void print_solution(const Preprocessor &input) const;
};
