#include "cardsatLS.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

using namespace std;
using namespace LocalSearch;

int CardSATLS::swap_bms_sample_count(int clause_length, int cap) {
  return std::min((clause_length - 1) / 2 + 1, cap);
}

namespace {

enum ScoreStackUpdate { kNoStackUpdate, kAfterScoreIncrease, kAfterScoreDecrease };

void add_to_goodvar_stack(CardSATLS *solver, int v) {
  solver->already_in_goodvar_stack[v] = solver->goodvar_stack_fill_pointer;
  solver->goodvar_stack[solver->goodvar_stack_fill_pointer++] = v;
}

void remove_from_goodvar_stack(CardSATLS *solver, int v) {
  int top_v = solver->goodvar_stack[--solver->goodvar_stack_fill_pointer];
  solver->goodvar_stack[solver->already_in_goodvar_stack[v]] = top_v;
  solver->already_in_goodvar_stack[top_v] = solver->already_in_goodvar_stack[v];
  solver->already_in_goodvar_stack[v] = -1;
}

void update_score(CardSATLS *solver, int v, int delta,
                  ScoreStackUpdate stack_update) {
  solver->score[v] += delta;
  if (stack_update == kAfterScoreIncrease) {
    if (solver->score[v] > 0 && solver->already_in_goodvar_stack[v] == -1) {
      add_to_goodvar_stack(solver, v);
    }
  } else if (stack_update == kAfterScoreDecrease) {
    if (solver->already_in_goodvar_stack[v] != -1 && solver->score[v] <= 0) {
      remove_from_goodvar_stack(solver, v);
    }
  }
}

void update_clause_neighbor_scores(CardSATLS *solver, int c, int flipvar,
                                   int old_sat_count, int new_sat_count,
                                   bool maintain_goodvar_stack) {
  const int threshold = solver->clause_true_lit_thres[c];
  const int unit_weight = solver->unit_weight[c];
  const bool increasing = new_sat_count > old_sat_count;
  const bool update_true_literals =
      increasing ? old_sat_count == threshold : new_sat_count == threshold;
  const int delta =
      increasing ? (update_true_literals ? unit_weight : -unit_weight)
                 : (update_true_literals ? -unit_weight : unit_weight);
  const lit *row = solver->clause_lit[c];
  const int row_size = solver->clause_lit_count[c];

  for (int j = 0; j < row_size; j++) {
    int v = row[j].var_num;
    if (v == flipvar) {
      continue;
    }

    bool lit_true = row[j].sense == solver->cur_soln[v];
    if (lit_true != update_true_literals) {
      continue;
    }

    ScoreStackUpdate stack_update = kNoStackUpdate;
    if (maintain_goodvar_stack) {
      stack_update = delta > 0 ? kAfterScoreIncrease : kAfterScoreDecrease;
    }
    update_score(solver, v, delta, stack_update);
  }
}

void update_count_for_occurrence(CardSATLS *solver, int flipvar,
                                 const lit &occurrence) {
  const int c = occurrence.clause_num;
  const int old_sat_count = solver->sat_count[c];
  const int new_sat_count =
      old_sat_count + (solver->cur_soln[flipvar] == occurrence.sense ? 1 : -1);
  const int threshold = solver->clause_true_lit_thres[c];
  if (old_sat_count < threshold && new_sat_count >= threshold) {
    solver->sat(c);
  } else if (old_sat_count >= threshold && new_sat_count < threshold) {
    solver->unsat(c);
  }
  solver->sat_count[c] = new_sat_count;
}

void update_one_flip_occurrence(CardSATLS *solver, int flipvar,
                                const lit &occurrence,
                                bool maintain_goodvar_stack) {
  const int c = occurrence.clause_num;
  const int old_sat_count = solver->sat_count[c];
  const int new_sat_count =
      old_sat_count + (solver->cur_soln[flipvar] == occurrence.sense ? 1 : -1);
  const int threshold = solver->clause_true_lit_thres[c];
  if (old_sat_count == threshold || new_sat_count == threshold) {
    update_clause_neighbor_scores(solver, c, flipvar, old_sat_count,
                                  new_sat_count, maintain_goodvar_stack);
  }
  update_count_for_occurrence(solver, flipvar, occurrence);
}

void flip_update_score_multi_impl(CardSATLS *solver, int flipvar,
                                  bool maintain_goodvar_stack) {
  for (int i = 0; i < solver->var_lit_count[flipvar]; ++i) {
    update_one_flip_occurrence(solver, flipvar, solver->var_lit[flipvar][i],
                               maintain_goodvar_stack);
  }
}

bool has_repeated_clause_occurrence(const CardSATLS *solver, int variable) {
  for (int i = 1; i < solver->var_lit_count[variable]; ++i) {
    if (solver->var_lit[variable][i - 1].clause_num ==
        solver->var_lit[variable][i].clause_num) {
      return true;
    }
  }
  return false;
}

void accumulate_delayed_score(CardSATLS *solver, int variable, int delta,
                              int &touched_count) {
  if (delta == 0) {
    return;
  }
  if (solver->neighbor_flag[variable] == 0) {
    solver->neighbor_flag[variable] = 1;
    solver->weight_touched_vars[touched_count++] = variable;
    solver->weight_delta_score[variable] = delta;
  } else {
    solver->weight_delta_score[variable] += delta;
  }
}

void update_shared_pair_scores(CardSATLS *solver, int c, int first_var,
                               int second_var, int old_sat_count,
                               int middle_sat_count, int final_sat_count,
                               int &delayed_touched_count) {
  const int threshold = solver->clause_true_lit_thres[c];
  const bool update_first =
      old_sat_count == threshold || middle_sat_count == threshold;
  const bool update_second =
      middle_sat_count == threshold || final_sat_count == threshold;
  if (!update_first && !update_second) {
    return;
  }

  const int unit_weight = solver->unit_weight[c];
  bool first_true_phase = false;
  int first_delta = 0;
  if (update_first) {
    const bool increasing = middle_sat_count > old_sat_count;
    first_true_phase =
        increasing ? old_sat_count == threshold : middle_sat_count == threshold;
    first_delta = increasing ? (first_true_phase ? unit_weight : -unit_weight)
                             : (first_true_phase ? -unit_weight : unit_weight);
  }

  bool second_true_phase = false;
  int second_delta = 0;
  if (update_second) {
    const bool increasing = final_sat_count > middle_sat_count;
    second_true_phase = increasing ? middle_sat_count == threshold
                                   : final_sat_count == threshold;
    second_delta = increasing
                       ? (second_true_phase ? unit_weight : -unit_weight)
                       : (second_true_phase ? -unit_weight : unit_weight);
  }

  const lit *row = solver->clause_lit[c];
  for (int j = 0; j < solver->clause_lit_count[c]; ++j) {
    const int v = row[j].var_num;
    if (update_first && v != first_var) {
      const bool lit_true = row[j].sense == solver->cur_soln[v];
      if (lit_true == first_true_phase) {
        update_score(solver, v, first_delta, kNoStackUpdate);
      }
    }

    if (update_second && v != second_var) {
      const bool lit_true = row[j].sense == solver->cur_soln[v];
      if (lit_true == second_true_phase) {
        accumulate_delayed_score(solver, v, second_delta,
                                 delayed_touched_count);
      }
    }
  }
}

} // namespace

void CardSATLS::init_score_multi() {
  int sense, weight, v, c;
  for (v = 1; v <= num_vars; v++) {
    score[v] = 0;
    for (int i = 0; i < var_lit_count[v]; ++i) {
      c = var_lit[v][i].clause_num;
      sense = var_lit[v][i].sense;
      weight = 1;

      if (sat_count[c] < clause_true_lit_thres[c]) {
        // UNSAT
        if (sense != cur_soln[v]) {
          score[v] += violation_make_contribution(
              unit_weight[c], clause_true_lit_thres[c] - sat_count[c]);
        } else {
          score[v] -= violation_make_contribution(
              unit_weight[c], clause_true_lit_thres[c] - sat_count[c] + weight);
        }
      } else if (sat_count[c] >= clause_true_lit_thres[c]) {
        // SAT
        if (sense == cur_soln[v]) {
          score[v] -= violation_make_contribution(
              unit_weight[c], clause_true_lit_thres[c] - sat_count[c] + weight);
        }
      }
    }
  }
  return;
}

void CardSATLS::flip_update_score_multi(int flipvar) {
  flip_update_score_multi_impl(this, flipvar, false);
}

void CardSATLS::flip_update_score_no_neighbor_multi(int flipvar) {
  flip_update_score_multi_impl(this, flipvar, true);
}

void CardSATLS::update_weight_score_multi(int c) {
  int i = 0, v = 0;
  for (i = 0; i < clause_lit_count[c]; i++) {
    v = clause_lit[c][i].var_num;
    if (clause_lit[c][i].sense != cur_soln[v]) {
      score[v] += violation_make_contribution(
          static_cast<int>(h_inc), clause_true_lit_thres[c] - sat_count[c]);
      if (score[v] > 0 && already_in_goodvar_stack[v] == -1) {
        already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
        mypush(v, goodvar_stack);
      }
    } else {
      score[v] -= violation_make_contribution(
          static_cast<int>(h_inc),
          clause_true_lit_thres[c] - sat_count[c] + 1);
      if (already_in_goodvar_stack[v] != -1 && score[v] <= 0) {
        int top_v = mypop(goodvar_stack);
        goodvar_stack[already_in_goodvar_stack[v]] = top_v;
        already_in_goodvar_stack[top_v] = already_in_goodvar_stack[v];
        already_in_goodvar_stack[v] = -1;
      }
    }
  }
  return;
}

int CardSATLS::select_var_after_update_weight() {
  int c, i, l, best_var, best_w, temp_l, v;

  c = hardunsat_stack[fast_rand() % hardunsat_stack_fill_pointer];
  l = 0;
  for (i = 0; i < clause_lit_count[c]; i++) {
    if (clause_lit[c][i].sense != cur_soln[clause_lit[c][i].var_num]) {
      temp_unsat[l].var_num = clause_lit[c][i].var_num;
      l++;
    }
  }

  if (l == 0) {
    best_var = clause_lit[c][0].var_num;
    for (i = 1; i < clause_lit_count[c]; i++) {
      v = clause_lit[c][i].var_num;
      if (score[best_var] < score[v]) {
        best_var = v;
      } else if (score[best_var] == score[v] &&
                 time_stamp[best_var] > time_stamp[v]) {
        best_var = v;
      }
    }
    return best_var;
  }

  if ((fast_rand() % MY_RAND_MAX_INT) < rwprob_threshold) {
    return temp_unsat[fast_rand() % l].var_num;
  } else {
    v = temp_unsat[0].var_num;
    best_w = score[v];
    temp_l = 0;
    temp_array[temp_l++] = v;
    for (i = 1; i < l; i++) {
      v = temp_unsat[i].var_num;
      if (best_w < score[v]) {
        temp_l = 0;
        temp_array[temp_l++] = v;
        best_w = score[v];
      } else if (best_w == score[v] &&
                 time_stamp[v] < time_stamp[temp_array[0]]) {
        temp_l = 0;
        temp_array[temp_l++] = v;
      } else if (best_w == score[v] &&
                 time_stamp[v] == time_stamp[temp_array[0]]) {
        temp_array[temp_l++] = v;
      }
    }
    return temp_array[fast_rand() % temp_l];
  }
}

void CardSATLS::swap_vars() {
  if (cardinalitysat_stack_fill_pointer <= 0) {
    return;
  }

  int sel_c =
      cardinalitysat_stack[fast_rand() % cardinalitysat_stack_fill_pointer];
  const int row_size = clause_lit_count[sel_c];
  if (row_size <= 0) {
    return;
  }

  const int probes = swap_bms_sample_count(row_size, swap_bms_cap);
  const int start = fast_rand() % row_size;
  int stride = 1;
  if (row_size > 1) {
    // Either direction is coprime with every row size, giving a bounded
    // without-replacement walk with O(1) setup.
    stride = (fast_rand() & 1u) == 0 ? 1 : row_size - 1;
  }
  int best_true_var = 0;
  int best_false_var = 0;

  auto consider = [&](const lit &candidate) {
    const int v = candidate.var_num;
    int &best = cur_soln[v] == candidate.sense ? best_true_var : best_false_var;
    if (best == v) {
      return;
    }
    if (best == 0 || score[v] > score[best] ||
        (score[v] == score[best] && time_stamp[v] < time_stamp[best]) ||
        (score[v] == score[best] && time_stamp[v] == time_stamp[best] &&
         fast_rand() % 2 == 1)) {
      best = v;
    }
  };

  for (int i = 0; i < probes; ++i) {
    const int probe = static_cast<int>(
        (static_cast<uint64_t>(start) + static_cast<uint64_t>(i) * stride) %
        row_size);
    consider(clause_lit[sel_c][probe]);
  }

  if (best_true_var == 0 || best_false_var == 0 ||
      best_true_var == best_false_var) {
    return;
  }

  flip_pair_atomic(best_true_var, best_false_var);

  time_stamp[best_true_var] = step;
  time_stamp[best_false_var] = step;
}

void CardSATLS::flip_pair_atomic(int first_var, int second_var) {
  if (first_var <= 0 || second_var <= 0 || first_var > num_vars ||
      second_var > num_vars || first_var == second_var) {
    return;
  }

  // On-the-fly score maintenance mutates the good-variable stack after every
  // delta.  Keep the exact ordered update there and for unusual repeated
  // occurrences; both paths remain allocation-free.
  if (!materialized_neighbor_mode || sat_var == nullptr ||
      has_repeated_clause_occurrence(this, first_var) ||
      has_repeated_clause_occurrence(this, second_var)) {
    flip(first_var);
    flip(second_var);
    return;
  }

  // Occurrence lists are ordered by clause id.  Mark their intersection in
  // O(deg(first)+deg(second)); sat_var records the second literal's phase.
  int first_index = 0;
  int second_index = 0;
  int shared_count = 0;
  while (first_index < var_lit_count[first_var] &&
         second_index < var_lit_count[second_var]) {
    const int first_clause = var_lit[first_var][first_index].clause_num;
    const int second_clause = var_lit[second_var][second_index].clause_num;
    if (first_clause < second_clause) {
      ++first_index;
    } else if (second_clause < first_clause) {
      ++second_index;
    } else {
      temp_array[shared_count++] = first_clause;
      sat_var[first_clause] = var_lit[second_var][second_index].sense ? 2 : 1;
      ++first_index;
      ++second_index;
    }
  }

  if (shared_count == 0) {
    flip(first_var);
    flip(second_var);
    return;
  }

  const int old_first_score = score[first_var];
  cur_soln[first_var] = 1 - cur_soln[first_var];
  int delayed_touched_count = 0;
  for (int i = 0; i < var_lit_count[first_var]; ++i) {
    const lit &occurrence = var_lit[first_var][i];
    const int c = occurrence.clause_num;
    if (sat_var[c] == 0) {
      update_one_flip_occurrence(this, first_var, occurrence, false);
      continue;
    }

    const int old_sat_count = sat_count[c];
    const int middle_sat_count =
        old_sat_count + (cur_soln[first_var] == occurrence.sense ? 1 : -1);
    const bool second_sense = sat_var[c] == 2;
    const int final_sat_count =
        middle_sat_count +
        ((1 - cur_soln[second_var]) == second_sense ? 1 : -1);
    update_shared_pair_scores(this, c, first_var, second_var, old_sat_count,
                              middle_sat_count, final_sat_count,
                              delayed_touched_count);
    update_count_for_occurrence(this, first_var, occurrence);
  }
  score[first_var] = -old_first_score;
  update_goodvarstack(first_var);

  const int old_second_score = score[second_var];
  cur_soln[second_var] = 1 - cur_soln[second_var];
  for (int i = 0; i < delayed_touched_count; ++i) {
    const int v = weight_touched_vars[i];
    update_score(this, v, weight_delta_score[v], kNoStackUpdate);
    weight_delta_score[v] = 0;
    neighbor_flag[v] = 0;
  }

  for (int i = 0; i < var_lit_count[second_var]; ++i) {
    const lit &occurrence = var_lit[second_var][i];
    if (sat_var[occurrence.clause_num] == 0) {
      update_one_flip_occurrence(this, second_var, occurrence, false);
    } else {
      update_count_for_occurrence(this, second_var, occurrence);
    }
  }
  score[second_var] = -old_second_score;
  update_goodvarstack(second_var);

  for (int i = 0; i < shared_count; ++i) {
    sat_var[temp_array[i]] = 0;
  }
}
