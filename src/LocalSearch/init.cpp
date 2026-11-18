#include "cardsatLS.hpp"

#include <algorithm>
#include <cstdlib>

using namespace LocalSearch;

void CardSATLS::init_assignment() {
  switch (init_mode) {
  case 0:
    for (int v = 1; v <= num_vars; ++v)
      cur_soln[v] = std::rand() % 2;
    break;
  case 3:
  case 4:
    std::fill_n(cur_soln + 1, num_vars, init_mode == 4);
    break;
  default:
    // Modes 1 and 2 share the same restart rule and start with all variables false.
    if (tries == 1) {
      std::fill_n(cur_soln + 1, num_vars, 0);
    } else {
      for (int v = 1; v <= num_vars; ++v) {
        if (time_stamp[v] > backbone_threshold)
          cur_soln[v] = 0;
      }
    }
    break;
  }
  std::fill_n(time_stamp + 1, num_vars, 0);
}

void CardSATLS::prepare_noble_count() {
  noble_true_xor.clear();
  noble_eligible.clear();
  if (!enable_noble_count) {
    return;
  }
  noble_true_xor.assign(num_clauses, 0);
  noble_eligible.resize(num_clauses);
  for (int c = 0; c < num_clauses; ++c) {
    noble_eligible[c] = clause_true_lit_thres[c] == 1;
  }
  // Occurrences are stored in clause order. Repeated variables, including
  // opposite signs, must keep the existing score-update path.
  for (int v = 1; v <= num_vars; ++v) {
    for (int i = 1; i < var_lit_count[v]; ++i) {
      const int c = var_lit[v][i].clause_num;
      if (c == var_lit[v][i - 1].clause_num) {
        noble_eligible[c] = false;
      }
    }
  }
}

void CardSATLS::allocate_memory() {
  const std::size_t variables = static_cast<std::size_t>(num_vars) + 1;
  const std::size_t clauses = num_clauses;

  var_lit = new lit *[variables];
  var_lit_count = new int[variables]();
  clause_lit = new lit *[clauses];
  clause_lit_count = new int[clauses];
  clause_true_lit_thres = new int[clauses];

  score = new int[variables];
  time_stamp = new int[variables];
  cur_soln = new int[variables];
  var_neighbor = new int *[variables]();
  var_neighbor_count = new int[variables]();

  unit_weight = new int[clauses];
  weight_delta_score = new int[variables]();
  weight_touched_vars = new int[variables];
  weight_is_touched = new bool[variables]();
  sat_count = new int[clauses];
  sat_var = nullptr;
  temp_array = nullptr;

  hardunsat_stack = new int[clauses];
  index_in_hardunsat_stack = new int[clauses];
  cardinalitysat_stack = new int[clauses];
  index_in_cardinalitysat_stack = new int[clauses];
  goodvar_stack = new int[variables];
  already_in_goodvar_stack = new int[variables];
}

void CardSATLS::free_memory() {
  if (!var_lit)
    return;

  if (literal_storage) {
    delete[] literal_storage;
    literal_storage = nullptr;
  } else {
    // CCDCL supplies separately allocated clause and occurrence lists.
    for (int c = 0; c < num_clauses; ++c)
      delete[] clause_lit[c];
    for (int v = 1; v <= num_vars; ++v)
      delete[] var_lit[v];
  }
  for (int v = 1; v <= num_vars; ++v)
    delete[] var_neighbor[v];

  delete[] var_lit;
  var_lit = nullptr;
  delete[] var_lit_count;
  delete[] clause_lit;
  delete[] clause_lit_count;
  delete[] clause_true_lit_thres;
  delete[] score;
  delete[] time_stamp;
  delete[] cur_soln;
  delete[] var_neighbor;
  delete[] var_neighbor_count;
  delete[] unit_weight;
  delete[] weight_delta_score;
  delete[] weight_touched_vars;
  delete[] weight_is_touched;
  delete[] sat_count;
  delete[] sat_var;
  delete[] temp_array;
  delete[] hardunsat_stack;
  delete[] index_in_hardunsat_stack;
  delete[] cardinalitysat_stack;
  delete[] index_in_cardinalitysat_stack;
  delete[] goodvar_stack;
  delete[] already_in_goodvar_stack;
}
