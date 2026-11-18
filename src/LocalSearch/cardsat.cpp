#include "cardsatLS.hpp"
#include "option.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <unistd.h>

using namespace LocalSearch;

int CardSATLS::solve() {
  settings();
  start_timing();
  local_search();
  return verify_sol();
}

void CardSATLS::local_search() {
  for (tries = 1; tries < max_tries; ++tries) {
    init_assignment();
    init_local_search();
    minimum_hard_unsat_nb = std::min(minimum_hard_unsat_nb, unsat_count);
    int res = local_search_flip();
    if (res == 10) {
      return;
    }
  }
}

int CardSATLS::local_search_flip() {
  int best_hard_unsat_nb = unsat_count;
  unsigned int non_improve_flips = 0;
  for (step = 1; step < max_flips && unsat_count > 0 &&
                 non_improve_flips < max_non_improve_flip;
       ++step) {
    total_step++;
    if (enable_swap && satisfied_cardinality_count > 0 &&
        non_improve_flips >= swap_non_improve_flip &&
        (fast_rand() % MY_RAND_MAX_INT) < swapprob_threshold) {
      swap_vars();
    } else {
      int flipvar = pick_var();
      flip(flipvar);
      time_stamp[flipvar] = step;
    }

    if (unsat_count < best_hard_unsat_nb) {
      best_hard_unsat_nb = unsat_count;
      non_improve_flips = 0;
    } else {
      ++non_improve_flips;
    }
    minimum_hard_unsat_nb = std::min(minimum_hard_unsat_nb, unsat_count);
  }
  if (unsat_count == 0) {
    opt_time = get_runtime();
    return 10;
  }
  return 0;
}

void CardSATLS::settings() {
  total_step = 0;
  minimum_hard_unsat_nb = std::numeric_limits<int>::max();
  max_tries = 2000000000;

  delta_total_weight = 0;
  ave_weight = 1;
  enable_weight_transfer =
      solver_options.mode == 0 && solver_options.ls_weight_transfer;
  weight_update_period = solver_options.ls_weight_update_period;
  weight_work_budget = solver_options.ls_weight_work_budget;
  p_scale = 0.3;
  const float ratio = float(num_clauses) / num_vars;
  const bool use_offset = q_init ? q_scale < 0.5f : !(ratio <= 15);
  q_scale = use_offset ? 0.7f : 0.0f;
  constexpr int smoothing_threshold = 300;
  scale_ave = (smoothing_threshold + 1) * q_scale;
  q_init = 1;

  const float rdprob = solver_options.ls_rdprob;
  const float rwprob = solver_options.ls_rwprob;
  hd_count_threshold = num_vars > 2000 ? solver_options.ls_hd_count_large
                                       : solver_options.ls_hd_count_small;
  swap_bms_cap = solver_options.ls_swap_bms_cap;
  enable_joint_swap =
      solver_options.mode == 0 && solver_options.ls_joint_swap != 0;
  enable_noble_count =
      solver_options.mode == 0 && solver_options.ls_noble_count != 0;
  prepare_noble_count();
  joint_swap_samples = solver_options.ls_joint_swap_samples;
  joint_swap_work_budget = solver_options.ls_joint_swap_work_budget;
  rwprob_threshold = static_cast<int>(rwprob * MY_RAND_MAX_INT);
  rdprob_threshold = static_cast<int>(rdprob * MY_RAND_MAX_INT);
  swapprob_threshold = static_cast<int>(swapprob * MY_RAND_MAX_INT);
  m_prng_state = solver_options.seeds ? solver_options.seeds : 1;

  constexpr uint64_t entries_per_gib = (uint64_t{1} << 30) / sizeof(int);
  prepare_neighbors(solver_options.ls_neighbor_budget_gib * entries_per_gib);
}

void CardSATLS::init_local_search() {
  weight_update_tick = 0;
  unsat_count = 0;
  satisfied_cardinality_count = 0;
  std::fill_n(score + 1, num_vars, 0);

  for (int c = 0; c < num_clauses; ++c) {
    unit_weight[c] = 1;
    const lit *row = clause_lit[c];
    const int length = clause_lit_count[c];
    const int threshold = clause_true_lit_thres[c];
    const bool track_xor = !noble_eligible.empty() && noble_eligible[c];
    int count = 0;
    if (track_xor) {
      uint32_t true_xor = 0;
      for (int j = 0; j < length; ++j) {
        if (cur_soln[row[j].var_num] == row[j].sense) {
          ++count;
          true_xor ^= noble_literal_id(row[j]);
        }
      }
      noble_true_xor[c] = true_xor;
    } else {
      for (int j = 0; j < length; ++j)
        count += cur_soln[row[j].var_num] == row[j].sense;
    }
    sat_count[c] = count;

    if (threshold > 1) {
      index_in_cardinalitysat_stack[c] = -1;
      if (count >= threshold) {
        index_in_cardinalitysat_stack[c] = satisfied_cardinality_count;
        cardinalitysat_stack[satisfied_cardinality_count++] = c;
      }
    }
    if (count < threshold)
      unsat(c);

    if (track_xor && count == 1) {
      --score[noble_true_xor[c] >> 1];
    } else if (count <= threshold) {
      for (int j = 0; j < length; ++j) {
        const int v = row[j].var_num;
        score[v] += clause_score_contribution(cur_soln[v] == row[j].sense,
                                              count, threshold, 1);
      }
    }
  }

  goodvar_count = 0;
  for (int v = 1; v <= num_vars; ++v) {
    if (score[v] > 0)
      add_goodvar(v);
    else
      already_in_goodvar_stack[v] = -1;
  }
}

int CardSATLS::verify_sol() {
  for (int c = 0; c < num_clauses; ++c) {
    int count = 0;
    for (int j = 0; j < clause_lit_count[c]; ++j) {
      if (cur_soln[clause_lit[c][j].var_num] == clause_lit[c][j].sense) {
        count++;
      }
    }
    if (count < clause_true_lit_thres[c]) {
      std::printf(
          "c verify solution is wrong in clause %d, with count %d and true "
          "lit thres %d\n",
          c, count, clause_true_lit_thres[c]);
      return 0;
    }
  }
  return 10;
}

void CardSATLS::unsat(int clause) {
  index_in_hardunsat_stack[clause] = unsat_count;
  hardunsat_stack[unsat_count++] = clause;

  if (clause_true_lit_thres[clause] > 1 &&
      index_in_cardinalitysat_stack[clause] != -1) {
    int index = index_in_cardinalitysat_stack[clause];
    int last_cardinality =
        cardinalitysat_stack[--satisfied_cardinality_count];
    cardinalitysat_stack[index] = last_cardinality;
    index_in_cardinalitysat_stack[last_cardinality] = index;
    index_in_cardinalitysat_stack[clause] = -1;
  }
}

void CardSATLS::sat(int clause) {
  const int last_unsat_clause = hardunsat_stack[--unsat_count];
  const int index = index_in_hardunsat_stack[clause];
  hardunsat_stack[index] = last_unsat_clause;
  index_in_hardunsat_stack[last_unsat_clause] = index;

  if (clause_true_lit_thres[clause] > 1) {
    index_in_cardinalitysat_stack[clause] = satisfied_cardinality_count;
    cardinalitysat_stack[satisfied_cardinality_count++] = clause;
  }
}

void CardSATLS::start_timing() { times(&start_time); }

double CardSATLS::get_runtime() {
  struct tms stop;
  times(&stop);
  return (double)(stop.tms_utime - start_time.tms_utime + stop.tms_stime -
                  start_time.tms_stime) /
         sysconf(_SC_CLK_TCK);
}
