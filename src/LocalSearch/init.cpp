#include "cardsatLS.hpp"

using namespace LocalSearch;
using namespace std;

namespace {

void init_assignment_with_value(CardSATLS *solver, int value) {
  for (int v = 1; v <= solver->num_vars; v++) {
    solver->cur_soln[v] = value;
    solver->time_stamp[v] = 0;
  }
}

void init_assignment_with_random_value(CardSATLS *solver) {
  for (int v = 1; v <= solver->num_vars; v++) {
    solver->cur_soln[v] = rand() % 2 ? 1 : 0;
    solver->time_stamp[v] = 0;
  }
}

void init_assignment_from_backbone(CardSATLS *solver) {
  for (int v = 1; v <= solver->num_vars; v++) {
    if (solver->time_stamp[v] > solver->backbone_threshold) {
      solver->cur_soln[v] = 0;
    }
    solver->time_stamp[v] = 0;
  }
}

} // namespace

void CardSATLS::set_init_method(int init_mode) {
  switch (init_mode) {
  case 0:
    init_assignment = [this]() { this->init_assignment_with_random(); };
    break;
  case 1:
    init_assignment = [this]() { this->init_assignment_with_backbone(); };
    break;
  case 2:
    init_assignment = [this]() { this->init_assignment_with_best(); };
    break;
  case 3:
    init_assignment = [this]() { this->init_assignment_with_false(); };
    break;
  case 4:
    init_assignment = [this]() { this->init_assignment_with_true(); };
    break;
  default:
    init_assignment = [this]() { this->init_assignment_with_backbone(); };
    break;
  }
}

void CardSATLS::init_assignment_with_false() {
  init_assignment_with_value(this, 0);
}

void CardSATLS::init_assignment_with_true() {
  init_assignment_with_value(this, 1);
}

void CardSATLS::init_assignment_with_random() {
  init_assignment_with_random_value(this);
}

void CardSATLS::init_assignment_with_backbone() {
  // The first try seeds the fake-backbone history with an all-false model.
  if (tries == 1) {
    init_assignment_with_false();
    return;
  }
  init_assignment_from_backbone(this);
}

void CardSATLS::init_assignment_with_best() { init_assignment_with_backbone(); }

void CardSATLS::allocate_memory() {
  int malloc_var_length = num_vars + 10;
  int malloc_clause_length = num_clauses + 10;

  temp_unsat = new temp_var[malloc_var_length];
  temp_array = new int[malloc_clause_length];

  var_lit = new lit *[malloc_var_length];
  var_lit_count = new int[malloc_var_length]();
  clause_lit = new lit *[malloc_clause_length];
  clause_lit_count = new int[malloc_clause_length]();
  clause_true_lit_thres = new int[malloc_clause_length];

  score = new int[malloc_var_length];
  var_neighbor = new int *[malloc_var_length]();
  var_neighbor_count = new int[malloc_var_length]();
  time_stamp = new int[malloc_var_length];
  neighbor_flag = new int[malloc_var_length]();
  temp_neighbor = new int[malloc_var_length];

  unit_weight = new int[malloc_clause_length];
  weight_delta_score = new int[malloc_var_length]();
  weight_touched_vars = new int[malloc_var_length];
  weight_is_touched = new bool[malloc_var_length]();
  sat_count = new int[malloc_clause_length];
  sat_var = new int[malloc_clause_length]();

  hardunsat_stack = new int[malloc_clause_length];
  index_in_hardunsat_stack = new int[malloc_clause_length];

  cardinalitysat_stack = new int[malloc_clause_length];
  index_in_cardinalitysat_stack = new int[malloc_clause_length];

  goodvar_stack = new int[malloc_var_length];
  already_in_goodvar_stack = new int[malloc_var_length];

  cur_soln = new int[malloc_var_length];
}

void CardSATLS::release_on_the_fly_memory() {
  if (weight_delta_score == neighbor_flag) {
    return;
  }

  const int scratch_capacity = std::max(max_clause_length, 1);
  temp_var *small_temp_unsat = new temp_var[scratch_capacity];
  int *small_temp_array = new int[scratch_capacity];
  delete[] temp_unsat;
  delete[] temp_array;
  temp_unsat = small_temp_unsat;
  temp_array = small_temp_array;

  delete[] sat_var;
  sat_var = nullptr;

  delete[] weight_delta_score;
  delete[] weight_touched_vars;
  weight_delta_score = neighbor_flag;
  weight_touched_vars = temp_neighbor;
  std::fill_n(weight_delta_score, num_vars + 10, 0);
}

void CardSATLS::free_memory() {
  int i;
  for (i = 0; i < num_clauses; i++)
    delete[] clause_lit[i];

  for (i = 1; i <= num_vars; ++i) {
    delete[] var_lit[i];
    delete[] var_neighbor[i];
  }
  delete[] temp_array;
  delete[] temp_unsat;
  delete[] var_lit;
  delete[] var_lit_count;
  delete[] clause_lit;
  delete[] clause_lit_count;
  delete[] clause_true_lit_thres;

  delete[] score;
  delete[] var_neighbor;
  delete[] var_neighbor_count;
  delete[] time_stamp;

  delete[] unit_weight;
  if (weight_delta_score != neighbor_flag)
    delete[] weight_delta_score;
  if (weight_touched_vars != temp_neighbor)
    delete[] weight_touched_vars;
  delete[] weight_is_touched;
  delete[] sat_count;
  delete[] sat_var;

  delete[] neighbor_flag;
  delete[] temp_neighbor;

  delete[] hardunsat_stack;
  delete[] index_in_hardunsat_stack;

  delete[] cardinalitysat_stack;
  delete[] index_in_cardinalitysat_stack;

  delete[] goodvar_stack;
  delete[] already_in_goodvar_stack;

  delete[] cur_soln;
}
