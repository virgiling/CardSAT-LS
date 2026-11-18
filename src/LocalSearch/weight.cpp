#include "cardsatLS.hpp"

#include <cassert>

using namespace LocalSearch;

void CardSATLS::increase_weights() {
  int touched_vars_count = 0;
  for (int i = 0; i < unsat_count; ++i) {
    const int c = hardunsat_stack[i];
    ++unit_weight[c];
    assert(sat_count[c] < clause_true_lit_thres[c]);
    for (int j = 0; j < clause_lit_count[c]; ++j) {
      const int v = clause_lit[c][j].var_num;
      if (!weight_is_touched[v]) {
        weight_is_touched[v] = true;
        weight_touched_vars[touched_vars_count++] = v;
      }

      const bool lit_true = clause_lit[c][j].sense == cur_soln[v];
      weight_delta_score[v] += lit_true ? -1 : 1;
    }
  }

  apply_weight_score_deltas(touched_vars_count);
  delta_total_weight += unsat_count;

  if (delta_total_weight >= num_clauses) {
    ave_weight += 1;
    delta_total_weight -= num_clauses;
  }
}

void CardSATLS::apply_weight_score_deltas(int touched_count) {
  for (int i = 0; i < touched_count; ++i) {
    const int v = weight_touched_vars[i];
    int delta = weight_delta_score[v];
    weight_delta_score[v] = 0;
    weight_is_touched[v] = false;
    if (delta == 0) {
      continue;
    }
    apply_weight_score_delta(v, delta);
  }
}

void CardSATLS::apply_weight_score_delta(int v, int delta) {
  score[v] += delta;
  if (delta > 0 && score[v] > 0 && already_in_goodvar_stack[v] == -1)
    add_goodvar(v);
  else if (delta < 0 && score[v] <= 0 && already_in_goodvar_stack[v] != -1)
    remove_goodvar(v);
}

void CardSATLS::smooth_weights() {
  for (int c = 0; c < num_clauses; ++c) {
    const int count = sat_count[c];
    const int threshold = clause_true_lit_thres[c];
    const int old_weight = unit_weight[c];
    if (old_weight == 1 && count < threshold)
      continue;
    unit_weight[c] = old_weight * p_scale + scale_ave;
    const int delta = unit_weight[c] - old_weight;
    if (delta == 0 || count > threshold)
      continue;
    for (int j = 0; j < clause_lit_count[c]; ++j) {
      const lit &occurrence = clause_lit[c][j];
      const int v = occurrence.var_num;
      const bool is_true = cur_soln[v] == occurrence.sense;
      const int change =
          clause_score_contribution(is_true, count, threshold, delta);
      if (change)
        apply_weight_score_delta(v, change);
    }
  }
}

void CardSATLS::update_clause_weights() { increase_weights(); }
