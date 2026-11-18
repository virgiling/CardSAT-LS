#include "cardsatLS.hpp"

#include <algorithm>
#include <limits>

using namespace LocalSearch;

namespace {

struct SwapGain {
  int64_t first = 0;
  int64_t second = 0;
  int64_t pair = 0;
};

int64_t deficit(int threshold, int64_t count) {
  return std::max<int64_t>(0, threshold - count);
}

// Occurrences are ordered by clause. Combine repeated literals before scoring.
bool evaluate_pair(const CardSATLS &s, int first, int second,
                   uint64_t &work_left, SwapGain &gain) {
  const int first_count = s.var_lit_count[first];
  const int second_count = s.var_lit_count[second];
  const uint64_t visits = static_cast<uint64_t>(first_count) + second_count;
  if (visits > work_left)
    return false;
  work_left -= visits;
  const lit *first_lits = s.var_lit[first];
  const lit *second_lits = s.var_lit[second];
  int i = 0;
  int j = 0;
  while (i < first_count || j < second_count) {
    const int c =
        std::min(i < first_count ? first_lits[i].clause_num : s.num_clauses,
                 j < second_count ? second_lits[j].clause_num : s.num_clauses);
    int first_delta = 0;
    int second_delta = 0;
    while (i < first_count && first_lits[i].clause_num == c)
      first_delta += s.cur_soln[first] == first_lits[i++].sense ? -1 : 1;
    while (j < second_count && second_lits[j].clause_num == c)
      second_delta += s.cur_soln[second] == second_lits[j++].sense ? -1 : 1;

    const int threshold = s.clause_true_lit_thres[c];
    const int64_t count = s.sat_count[c];
    const int64_t before = deficit(threshold, count);
    const int64_t weight = s.unit_weight[c];
    gain.first += weight * (before - deficit(threshold, count + first_delta));
    gain.second += weight * (before - deficit(threshold, count + second_delta));
    gain.pair +=
        weight *
        (before - deficit(threshold, count + first_delta + second_delta));
  }
  return true;
}

} // namespace

bool CardSATLS::try_joint_swap() {
  if (satisfied_cardinality_count <= 0 || joint_swap_work_budget <= 0) {
    return false;
  }
  const int c =
      cardinalitysat_stack[fast_rand() % satisfied_cardinality_count];
  const int row_size = clause_lit_count[c];
  if (row_size <= 1) {
    return false;
  }

  constexpr int max_samples = 64;
  const int sample_cap = std::clamp(joint_swap_samples, 1, max_samples);
  uint64_t work_left = joint_swap_work_budget;
  const int probes = static_cast<int>(
      std::min<uint64_t>(std::min(row_size, 8 * sample_cap), work_left));
  int pools[2][max_samples];
  int sizes[2] = {};
  for (int i = 0; i < probes; ++i) {
    --work_left;
    const lit &candidate = clause_lit[c][fast_rand() % row_size];
    const int v = candidate.var_num;
    const bool is_true = cur_soln[v] == candidate.sense;
    int *pool = pools[is_true];
    int &size = sizes[is_true];
    if (size < sample_cap && std::find(pool, pool + size, v) == pool + size)
      pool[size++] = v;
    if (sizes[0] == sample_cap && sizes[1] == sample_cap)
      break;
  }

  int first_best = 0;
  int second_best = 0;
  int64_t best_pair_gain = std::numeric_limits<int64_t>::min();
  int64_t best_single_gain = std::numeric_limits<int64_t>::min();
  for (int i = 0; i < sizes[1]; ++i) {
    const int first = pools[1][i];
    for (int j = 0; j < sizes[0]; ++j) {
      const int second = pools[0][j];
      SwapGain gain;
      if (first == second ||
          !evaluate_pair(*this, first, second, work_left, gain))
        continue;
      best_single_gain = std::max({best_single_gain, gain.first, gain.second});
      const int64_t age_sum =
          static_cast<int64_t>(time_stamp[first]) + time_stamp[second];
      const int64_t best_age_sum =
          first_best == 0 ? 0
                          : static_cast<int64_t>(time_stamp[first_best]) +
                                time_stamp[second_best];
      if (first_best == 0 || gain.pair > best_pair_gain ||
          (gain.pair == best_pair_gain && age_sum < best_age_sum) ||
          (gain.pair == best_pair_gain && age_sum == best_age_sum &&
           (fast_rand() & 1u) != 0)) {
        first_best = first;
        second_best = second;
        best_pair_gain = gain.pair;
      }
    }
  }

  if (first_best == 0 || best_pair_gain <= best_single_gain) {
    return false;
  }
  flip_pair_atomic(first_best, second_best);
  time_stamp[first_best] = step;
  time_stamp[second_best] = step;
  return true;
}

void CardSATLS::swap_vars() {
  if (enable_joint_swap) {
    if (!try_joint_swap() && unsat_count > 0) {
      const int v = pick_var();
      flip(v);
      time_stamp[v] = step;
    }
    return;
  }

  if (satisfied_cardinality_count <= 0) {
    return;
  }

  const int sel_c =
      cardinalitysat_stack[fast_rand() % satisfied_cardinality_count];
  const int row_size = clause_lit_count[sel_c];
  if (row_size <= 0) {
    return;
  }

  const int probes = std::min((row_size - 1) / 2 + 1, swap_bms_cap);
  int at = fast_rand() % row_size;
  const int direction = row_size > 1 && (fast_rand() & 1u) ? -1 : 1;
  int best_true_var = 0;
  int best_false_var = 0;
  for (int i = 0; i < probes; ++i) {
    const lit &candidate = clause_lit[sel_c][at];
    const int v = candidate.var_num;
    int &best = cur_soln[v] == candidate.sense ? best_true_var : best_false_var;
    if (v != best) {
      const int order = best ? compare_variables(v, best) : 1;
      if (order > 0 || (order == 0 && (fast_rand() & 1u)))
        best = v;
    }
    at += direction;
    if (at == row_size)
      at = 0;
    else if (at < 0)
      at = row_size - 1;
  }

  if (best_true_var == 0 || best_false_var == 0 ||
      best_true_var == best_false_var) {
    return;
  }

  flip_pair_atomic(best_true_var, best_false_var);

  time_stamp[best_true_var] = step;
  time_stamp[best_false_var] = step;
}
