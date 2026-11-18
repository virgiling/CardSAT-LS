#include "cardsatLS.hpp"

using namespace LocalSearch;

int CardSATLS::pick_var() {
  const int size = goodvar_count;
  if (size == 0) {
    update_clause_weights();
    return select_var_after_update_weight();
  }

  if (fast_rand() % MY_RAND_MAX_INT < rdprob_threshold)
    return goodvar_stack[fast_rand() % size];

  if (size < hd_count_threshold) {
    int best = goodvar_stack[0];
    for (int i = 1; i < size; ++i) {
      const int v = goodvar_stack[i];
      if (compare_variables(v, best) > 0)
        best = v;
    }
    return best;
  }

  int best = goodvar_stack[fast_rand() % size];
  for (int i = 1; i < hd_count_threshold; ++i) {
    const int v = goodvar_stack[fast_rand() % size];
    if (compare_variables(v, best) > 0)
      best = v;
  }
  return best;
}

int CardSATLS::select_var_after_update_weight() {
  const int c = hardunsat_stack[fast_rand() % unsat_count];
  const lit *row = clause_lit[c];
  const int length = clause_lit_count[c];
  int count = 0;
  for (int i = 0; i < length; ++i) {
    if (row[i].sense != cur_soln[row[i].var_num])
      temp_array[count++] = row[i].var_num;
  }

  if (count == 0) {
    int best = row[0].var_num;
    for (int i = 1; i < length; ++i) {
      if (compare_variables(row[i].var_num, best) > 0)
        best = row[i].var_num;
    }
    return best;
  }
  if (fast_rand() % MY_RAND_MAX_INT < rwprob_threshold)
    return temp_array[fast_rand() % count];

  int ties = 1;
  for (int i = 1; i < count; ++i) {
    const int v = temp_array[i];
    const int order = compare_variables(v, temp_array[0]);
    if (order > 0)
      ties = 0;
    if (order >= 0)
      temp_array[ties++] = v;
  }
  return temp_array[fast_rand() % ties];
}

void CardSATLS::update_goodvarstack(int flipvar) {
  for (int i = goodvar_count - 1; i >= 0; --i) {
    const int v = goodvar_stack[i];
    if (score[v] <= 0)
      remove_goodvar(v);
  }

  for (int i = 0; i < var_neighbor_count[flipvar]; ++i) {
    const int v = var_neighbor[flipvar][i];
    if (score[v] > 0 && already_in_goodvar_stack[v] == -1)
      add_goodvar(v);
  }
}
