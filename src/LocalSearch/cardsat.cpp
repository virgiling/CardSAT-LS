#include "cardsatLS.hpp"
#include "option.hpp"

#include <cstring>
#include <limits>

using namespace LocalSearch;
using namespace std;

bool CardSATLS::checked_neighbor_entry_budget(uint64_t budget_gib,
                                              uint64_t &entry_budget) {
  constexpr uint64_t kBytesPerGiB = uint64_t{1} << 30;
  if (budget_gib > std::numeric_limits<uint64_t>::max() / kBytesPerGiB) {
    return false;
  }
  entry_budget = (budget_gib * kBytesPerGiB) / sizeof(int);
  return true;
}

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
    minimum_hard_unsat_nb = std::min(minimum_hard_unsat_nb, hard_unsat_nb);
    int res = local_search_flip();
    if (res == 10) {
      return;
    }
  }
}

int CardSATLS::local_search_flip() {
  int best_hard_unsat_nb = hard_unsat_nb;
  unsigned int non_improve_flips = 0;
  for (step = 1; step < max_flips; ++step) {
    if (hard_unsat_nb == 0) {
      opt_time = get_runtime();
      return 10;
    }

    if (non_improve_flips >= max_non_improve_flip) {
      break;
    }

    total_step++;
    if (enable_swap && cardinalitysat_stack_fill_pointer > 0 &&
        non_improve_flips >= swap_non_improve_flip &&
        (fast_rand() % MY_RAND_MAX_INT) < swapprob_threshold) {
      swap_vars();
    } else {
      int flipvar = pick_var();
      flip(flipvar);
      time_stamp[flipvar] = step;
    }

    if (hard_unsat_nb < best_hard_unsat_nb) {
      best_hard_unsat_nb = hard_unsat_nb;
      non_improve_flips = 0;
    } else {
      ++non_improve_flips;
    }
    minimum_hard_unsat_nb = std::min(minimum_hard_unsat_nb, hard_unsat_nb);
  }
  return 0;
}

void CardSATLS::settings() {
  // steps
  total_step = 0;
  minimum_hard_unsat_nb = std::numeric_limits<int>::max();
  max_tries = 2000000000;

  delta_total_weight = 0;
  ave_weight = 1;
  threshold_weight = 300;
  p_scale = 0.3;
  ratio = float(num_clauses) / num_vars;
  if (q_init == 0) {
    if (ratio <= 15)
      q_scale = 0;
    else
      q_scale = 0.7;
  } else {
    if (q_scale < 0.5) // 0
      q_scale = 0.7;
    else
      q_scale = 0;
  }
  scale_ave = (threshold_weight + 1) * q_scale;
  q_init = 1;

  rdprob = OPT(ls_rdprob);
  rwprob = OPT(ls_rwprob);
  hd_count_threshold =
      num_vars > 2000 ? OPT(ls_hd_count_large) : OPT(ls_hd_count_small);
  h_inc = 1.0;
  swap_bms_cap = OPT(ls_swap_bms_cap);
  if (!checked_neighbor_entry_budget(
          static_cast<uint64_t>(OPT(ls_neighbor_budget_gib)),
          neighbor_entry_budget)) {
    std::fprintf(stderr, "c invalid neighbor memory budget\n");
    std::exit(1);
  }
  rwprob_threshold = static_cast<int>(rwprob * MY_RAND_MAX_INT);
  rdprob_threshold = static_cast<int>(rdprob * MY_RAND_MAX_INT);
  swapprob_threshold = static_cast<int>(swapprob * MY_RAND_MAX_INT);
  m_prng_state = OPT(seeds) ? OPT(seeds) : 1;

  select_var_after_update_weight_ptr = [this]() {
    return this->select_var_after_update_weight();
  };

  const uint64_t largest_clause_size = static_cast<uint64_t>(max_clause_length);
  const uint64_t min_entries_from_largest_clause =
      largest_clause_size > 1 ? largest_clause_size * (largest_clause_size - 1)
                              : 0;
  bool materialized = false;
  if (avg_neighbor_lit < 1e+7 &&
      min_entries_from_largest_clause <= neighbor_entry_budget) {
    materialized = build_neighbor_relation(neighbor_entry_budget);
  }
  if (materialized) {
    cout << "c neighbor_mode materialized\n";
    materialized_neighbor_mode = true;
    flip = [this](int v) { this->flip_with_neighbor(v); };
  } else {
    cout << "c neighbor_mode on_the_fly\n";
    materialized_neighbor_mode = false;
    release_on_the_fly_memory();
    flip = [this](int v) { this->flip_no_neighbor(v); };
  }
}

void CardSATLS::init_local_search() {
  int v, c;
  int i, j;

  // Initialize clause information
  for (i = 0; i < num_clauses; i++) {
    unit_weight[i] = 1.0;
  }
  // round

  // init stacks
  hard_unsat_nb = 0;
  hardunsat_stack_fill_pointer = 0;
  cardinalitysat_stack_fill_pointer = 0;

  for (c = 0; c < num_clauses; c++) {
    sat_count[c] = 0;
    for (j = 0; j < clause_lit_count[c]; ++j) {
      if (cur_soln[clause_lit[c][j].var_num] == clause_lit[c][j].sense) {
        sat_count[c] += 1;
      }
    }

    if (clause_true_lit_thres[c] > 1) {
      index_in_cardinalitysat_stack[c] = -1;
      if (sat_count[c] >= clause_true_lit_thres[c]) {
        index_in_cardinalitysat_stack[c] = cardinalitysat_stack_fill_pointer;
        mypush(c, cardinalitysat_stack);
      }
    }

    if (sat_count[c] < clause_true_lit_thres[c]) {
      unsat(c);
    }
  }

  /*figure out score*/
  init_score_multi();

  // init goodvars stack
  goodvar_stack_fill_pointer = 0;
  for (v = 1; v <= num_vars; v++) {
    if (score[v] > 0) {
      already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
      mypush(v, goodvar_stack);
    } else {
      already_in_goodvar_stack[v] = -1;
    }
  }
}

int CardSATLS::pick_var() {
  int i, v, r;
  int best_var;

  if (goodvar_stack_fill_pointer > 0) {
    if ((fast_rand() % MY_RAND_MAX_INT) < rdprob_threshold)
      return goodvar_stack[fast_rand() % goodvar_stack_fill_pointer];

    if (goodvar_stack_fill_pointer < hd_count_threshold) {
      best_var = goodvar_stack[0];
      for (i = 1; i < goodvar_stack_fill_pointer; ++i) {
        v = goodvar_stack[i];
        if (score[v] > score[best_var])
          best_var = v;
        else if (score[v] == score[best_var]) {
          if (time_stamp[v] < time_stamp[best_var])
            best_var = v;
        }
      }
      return best_var;
    } else {
      r = fast_rand() % goodvar_stack_fill_pointer;
      best_var = goodvar_stack[r];

      for (i = 1; i < hd_count_threshold; ++i) {
        r = fast_rand() % goodvar_stack_fill_pointer;
        v = goodvar_stack[r];
        if (score[v] > score[best_var])
          best_var = v;
        else if (score[v] == score[best_var]) {
          if (time_stamp[v] < time_stamp[best_var])
            best_var = v;
        }
      }
      return best_var;
    }
  }

  update_clause_weights();

  return select_var_after_update_weight_ptr();
}

void CardSATLS::update_goodvarstack(int flipvar) {
  int v;

  // remove the variables no longer goodvar in goodvar_stack
  for (int index = goodvar_stack_fill_pointer - 1; index >= 0; index--) {
    v = goodvar_stack[index];
    if (score[v] <= 0) {
      int top_v = mypop(goodvar_stack);
      goodvar_stack[index] = top_v;
      already_in_goodvar_stack[top_v] = index;
      already_in_goodvar_stack[v] = -1;
    }
  }

  // add goodvar
  for (int i = 0; i < var_neighbor_count[flipvar]; ++i) {
    v = var_neighbor[flipvar][i];
    if (score[v] > 0) {
      if (already_in_goodvar_stack[v] == -1) {
        already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
        mypush(v, goodvar_stack);
      }
    }
  }
}

void CardSATLS::flip_with_neighbor(int flipvar) {
  double org_flipvar_score = score[flipvar];
  cur_soln[flipvar] = 1 - cur_soln[flipvar];

  flip_update_score_multi(flipvar);

  // update information of flipvar
  score[flipvar] = -org_flipvar_score;
  update_goodvarstack(flipvar);
}

void CardSATLS::flip_no_neighbor(int flipvar) {
  double org_flipvar_score = score[flipvar];
  cur_soln[flipvar] = 1 - cur_soln[flipvar];

  flip_update_score_no_neighbor_multi(flipvar);

  // update information of flipvar
  score[flipvar] = -org_flipvar_score;
  if (already_in_goodvar_stack[flipvar] != -1) {
    int top_v = mypop(goodvar_stack);
    goodvar_stack[already_in_goodvar_stack[flipvar]] = top_v;
    already_in_goodvar_stack[top_v] = already_in_goodvar_stack[flipvar];
    already_in_goodvar_stack[flipvar] = -1;
  }
  return;
}

int CardSATLS::verify_sol() {
  int c, j;

  for (c = 0; c < num_clauses; ++c) {
    int count = 0;
    for (j = 0; j < clause_lit_count[c]; ++j) {
      if (cur_soln[clause_lit[c][j].var_num] == clause_lit[c][j].sense) {
        count++;
      }
    }
    if (count < clause_true_lit_thres[c]) {
      printf("c verify solution is wrong in clause %d, with count %d and true "
             "lit thres %d\n",
             c, count, clause_true_lit_thres[c]);
      return 0;
    }
  }
  return 10;
}

void CardSATLS::unsat(int clause) {
  index_in_hardunsat_stack[clause] = hardunsat_stack_fill_pointer;
  mypush(clause, hardunsat_stack);

  if (clause_true_lit_thres[clause] > 1 &&
      index_in_cardinalitysat_stack[clause] != -1) {
    // This is a cardinality unsat clause
    int index = index_in_cardinalitysat_stack[clause];
    int last_cardinality = mypop(cardinalitysat_stack);
    cardinalitysat_stack[index] = last_cardinality;
    index_in_cardinalitysat_stack[last_cardinality] = index;
    index_in_cardinalitysat_stack[clause] = -1;
  }

  hard_unsat_nb++;
}

void CardSATLS::sat(int clause) {
  int index, last_unsat_clause;

  last_unsat_clause = mypop(hardunsat_stack);
  index = index_in_hardunsat_stack[clause];
  hardunsat_stack[index] = last_unsat_clause;
  index_in_hardunsat_stack[last_unsat_clause] = index;

  if (clause_true_lit_thres[clause] > 1) {
    // This is a cardinality unsat clause
    index_in_cardinalitysat_stack[clause] = cardinalitysat_stack_fill_pointer;
    mypush(clause, cardinalitysat_stack);
  }

  hard_unsat_nb--;
}

void CardSATLS::start_timing() { times(&start_time); }

double CardSATLS::get_runtime() {
  struct tms stop;
  times(&stop);
  return (double)(stop.tms_utime - start_time.tms_utime + stop.tms_stime -
                  start_time.tms_stime) /
         sysconf(_SC_CLK_TCK);
}
