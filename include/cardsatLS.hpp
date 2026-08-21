#pragma once

#ifndef _cardsatLS_hpp_INCLUDED
#define _cardsatLS_hpp_INCLUDED
#include "preprocess.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/times.h>
#include <unistd.h>

namespace LocalSearch {

#define mypop(stack) stack[--stack##_fill_pointer]
#define mypush(item, stack) stack[stack##_fill_pointer++] = item

const float MY_RAND_MAX_FLOAT = 10000000.0;
const int MY_RAND_MAX_INT = 10000000;
const float BASIC_SCALE = 0.0000001; // 1.0f/MY_RAND_MAX_FLOAT;

// Shared scoring helpers used by both heuristic.cpp and weight.cpp.
inline int violation_make_contribution(int unit_weight, int gap) {
  return gap > 0 ? unit_weight : 0;
}

inline int clause_score_contribution(bool lit_true, int sat_count,
                                     int threshold, int unit_weight) {
  if (sat_count < threshold) {
    if (lit_true) {
      return -unit_weight;
    }
    return violation_make_contribution(unit_weight, threshold - sat_count);
  }
  if (lit_true && sat_count == threshold) {
    return -violation_make_contribution(unit_weight, 1);
  }
  return 0;
}

struct lit {
  int clause_num; // clause num, begin with 0
  int var_num;    // variable num, begin with 1
  bool sense;     // 1 for true literals, 0 for false literals.
};

struct temp_var {
  int var_num;
};

class CardSATLS {

public:
  // Lifecycle
  CardSATLS() = default;
  ~CardSATLS() = default;

public:
  // Temporary work buffers
  int *temp_array;
  struct tms start_time;
  temp_var *temp_unsat;

  // Search statistics
  int total_step;

  // size of the instance
  int num_vars;    // var index from 1 to num_vars
  int num_clauses; // clause index from 0 to num_clauses-1
  int max_clause_length;

  // steps and time
  int tries;
  int max_tries;
  unsigned int max_flips;
  unsigned int max_non_improve_flip;
  unsigned int swap_non_improve_flip;
  unsigned int step;

  int cutoff_time;
  double opt_time;

  // Literal arrays
  lit **var_lit;         // var_lit[i][j] means the j'th literal of variable i.
  int *var_lit_count;    // amount of literals of each variable
  lit **clause_lit;      // clause_lit[i][j] means the j'th literal of clause i.
  int *clause_lit_count; // amount of literals in each clause
  int *clause_true_lit_thres;

  // Variable state
  int *score;
  int *time_stamp;
  int **var_neighbor;
  int *var_neighbor_count;
  int *neighbor_flag;
  int *temp_neighbor;
  double avg_neighbor_lit = 0;

  // Clause weights
  int *unit_weight;
  int *weight_delta_score;
  int *weight_touched_vars;
  bool *weight_is_touched;
  int delta_total_weight;
  int ave_weight;
  int threshold_weight;
  float p_scale; // w=w*p+ave_w*q
  float q_scale;
  int scale_ave; // scale_ave==ave_weight*q_scale
  int q_init;
  float ratio;

  // Clause satisfaction state
  int *sat_count;
  int *sat_var;

  // unsat clauses stack
  int *hardunsat_stack; // store the falsified clause number
  int *
      index_in_hardunsat_stack; // which position is a clause in the unsat_stack
  int hardunsat_stack_fill_pointer;

  int *unsatvar_stack;
  int *index_in_unsatvar_stack;
  int unsatvar_stack_fill_pointer;
  int *unsat_app_count;

  int *cardinalitysat_stack; // store the cardinality of the unsat clauses
  int *index_in_cardinalitysat_stack; // which position is a clause in the
                                      // cardinalitysat_stack
  int cardinalitysat_stack_fill_pointer;

  // good decreasing variables (dscore>0)
  int *goodvar_stack;
  int goodvar_stack_fill_pointer;
  int *already_in_goodvar_stack;

  // Solution state
  int *cur_soln; // the current assignment, with 1's for True variables,
                 // and 0's for False variables
  int hard_unsat_nb;
  // Monotone diagnostic progress for tuning; it never affects search choices.
  int minimum_hard_unsat_nb;

  // Algorithm parameters
  float rwprob;
  float rdprob;
  int rwprob_threshold;
  int rdprob_threshold;
  int hd_count_threshold;
  double h_inc;
  int swap_bms_cap;
  uint64_t neighbor_entry_budget;
  double swapprob;
  int swapprob_threshold;
  int backbone_threshold;
  uint64_t m_prng_state;

  bool enable_swap;

  // Public API
  void parse_from_preprocess(Preprocessor *preprocessor);
  void write_instance();
  int solve();
  void local_search();
  int local_search_flip();
  void free_memory();
  void start_timing();
  double get_runtime();

  // Setup and verification
  bool build_neighbor_relation(uint64_t entry_budget);
  static bool checked_neighbor_entry_budget(uint64_t budget_gib,
                                            uint64_t &entry_budget);
  void allocate_memory();
  void release_on_the_fly_memory();
  int verify_sol();

  // Clause and weight updates
  void increase_weights();
  void smooth_weights();
  void update_clause_weights();
  void unsat(int clause);
  void sat(int clause);
  void init_local_search();

  // Variable selection and flipping
  void update_goodvarstack(int flipvar);
  void settings();
  void set_init_method(int init_mode);
  void init_score_multi();

  int pick_var();
  void swap_vars();
  void flip_pair_atomic(int first_var, int second_var);
  static int swap_bms_sample_count(int clause_length, int cap);
  inline uint32_t fast_rand() {
    m_prng_state ^= m_prng_state << 13;
    m_prng_state ^= m_prng_state >> 7;
    m_prng_state ^= m_prng_state << 17;
    return static_cast<uint32_t>(m_prng_state);
  }

  std::function<void(int flipvar)> flip;
  bool materialized_neighbor_mode = false;
  void flip_with_neighbor(int flipvar);
  void flip_no_neighbor(int flipvar);

  void flip_update_score_multi(int flipvar);
  void flip_update_score_no_neighbor_multi(int flipvar);

  void update_weight_score_multi(int c);

  // Assignment initialization
  std::function<void()> init_assignment;
  void init_assignment_with_false();
  void init_assignment_with_true();
  void init_assignment_with_random();
  void init_assignment_with_backbone();
  void init_assignment_with_best();

  // Post-weight-update selection
  std::function<int()> select_var_after_update_weight_ptr;
  int select_var_after_update_weight();
};

}; // namespace LocalSearch

#endif // _cardsatLS_hpp_INCLUDED
