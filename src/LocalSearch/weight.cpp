#include "cardsatLS.hpp"

#include <cassert>

using namespace LocalSearch;

namespace {
int find_weight_donor(const CardSATLS &s, int receiver) {
  int best_donor = -1;
  const int length = s.clause_lit_count[receiver];
  // Reserve a literal scan and at most one score write per literal.
  int64_t remaining = s.weight_work_budget - 2LL * length;
  int64_t donor_cost = 0;
  int best_weight = 1;
  int at = length ? s.step % length : 0;
  for (int n = 0; n < length; ++n) {
    if (--remaining < donor_cost)
      break;
    const lit &target = s.clause_lit[receiver][at];
    if (++at == length)
      at = 0;
    const int var = target.var_num;
    if (s.cur_soln[var] == target.sense)
      continue;
    const int count = s.var_lit_count[var];
    int pos = s.step % count;
    for (int j = 0; j < count; ++j) {
      if (--remaining < donor_cost)
        return best_donor;
      const lit &other = s.var_lit[var][pos];
      if (++pos == count)
        pos = 0;
      const int donor = other.clause_num;
      if (other.sense != target.sense || s.unit_weight[donor] <= best_weight ||
          s.sat_count[donor] < s.clause_true_lit_thres[donor])
        continue;
      const int64_t cost = 2LL * s.clause_lit_count[donor];
      if (cost > remaining)
        continue;
      best_donor = donor;
      donor_cost = cost;
      best_weight = s.unit_weight[donor];
    }
  }
  return best_donor;
}
} // namespace

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

void CardSATLS::accumulate_weight_delta(int c, int delta, int &touched_count) {
  unit_weight[c] += delta;
  for (int j = 0; j < clause_lit_count[c]; ++j) {
    const lit &occurrence = clause_lit[c][j];
    const int v = occurrence.var_num;
    const bool lit_true = cur_soln[v] == occurrence.sense;
    const int change = clause_score_contribution(
        lit_true, sat_count[c], clause_true_lit_thres[c], delta);
    if (!change)
      continue;
    if (!weight_is_touched[v]) {
      weight_is_touched[v] = true;
      weight_touched_vars[touched_count++] = v;
    }
    weight_delta_score[v] += change;
  }
}

bool CardSATLS::try_transfer_weights() {
  assert(unsat_count > 0);
  const int receiver = hardunsat_stack[step % unsat_count];
  const int donor = find_weight_donor(*this, receiver);
  if (donor < 0)
    return false;

  int touched_count = 0;
  accumulate_weight_delta(receiver, 1, touched_count);
  accumulate_weight_delta(donor, -1, touched_count);
  apply_weight_score_deltas(touched_count);
  return true;
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

void CardSATLS::update_clause_weights() {
  if (enable_weight_transfer && ++weight_update_tick == weight_update_period) {
    weight_update_tick = 0;
    if (try_transfer_weights())
      return;
  }
  increase_weights();
}
