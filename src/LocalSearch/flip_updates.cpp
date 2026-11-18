#include "cardsatLS.hpp"

using namespace LocalSearch;

namespace {

enum ScoreStackUpdate {
  kNoStackUpdate,
  kAfterScoreIncrease,
  kAfterScoreDecrease
};

void update_score(CardSATLS *solver, int v, int delta,
                  ScoreStackUpdate stack_update) {
  solver->score[v] += delta;
  if (stack_update == kAfterScoreIncrease) {
    if (solver->score[v] > 0 && solver->already_in_goodvar_stack[v] == -1) {
      solver->add_goodvar(v);
    }
  } else if (stack_update == kAfterScoreDecrease) {
    if (solver->already_in_goodvar_stack[v] != -1 && solver->score[v] <= 0) {
      solver->remove_goodvar(v);
    }
  }
}

void update_clause_neighbor_scores(CardSATLS *solver, const lit &occurrence,
                                   int old_sat_count, int new_sat_count,
                                   bool maintain_goodvar_stack) {
  const int c = occurrence.clause_num;
  const int flipvar = occurrence.var_num;
  const int threshold = solver->clause_true_lit_thres[c];
  const int unit_weight = solver->unit_weight[c];
  const bool increasing = new_sat_count > old_sat_count;
  // Leaving the threshold raises a neighbor's score. Reaching it lowers the
  // score.
  const bool update_true_literals =
      std::min(old_sat_count, new_sat_count) == threshold;
  const int delta = old_sat_count == threshold ? unit_weight : -unit_weight;
  ScoreStackUpdate stack_update = kNoStackUpdate;
  if (maintain_goodvar_stack) {
    stack_update = delta > 0 ? kAfterScoreIncrease : kAfterScoreDecrease;
  }
  const lit *row = solver->clause_lit[c];
  const int row_size = solver->clause_lit_count[c];
  if (!solver->noble_eligible.empty() && solver->noble_eligible[c]) {
    if (update_true_literals) {
      // For 1->2 the old XOR names the other true literal. For 2->1,
      // remove the flipped literal to obtain the remaining one.
      uint32_t unique = solver->noble_true_xor[c];
      if (!increasing)
        unique ^= CardSATLS::noble_literal_id(occurrence);
      update_score(solver, static_cast<int>(unique >> 1), delta, stack_update);
    } else {
      // On a 0<->1 transition every other literal is false.
      for (int j = 0; j < row_size; ++j) {
        const int v = row[j].var_num;
        if (v != flipvar)
          update_score(solver, v, delta, stack_update);
      }
    }
    return;
  }

  for (int j = 0; j < row_size; j++) {
    const int v = row[j].var_num;
    if (v == flipvar)
      continue;
    const bool lit_true = row[j].sense == solver->cur_soln[v];
    if (lit_true == update_true_literals)
      update_score(solver, v, delta, stack_update);
  }
}

void update_count_for_occurrence(CardSATLS *solver, const lit &occurrence,
                                 int old_sat_count, int new_sat_count) {
  const int c = occurrence.clause_num;
  const int threshold = solver->clause_true_lit_thres[c];
  if (old_sat_count < threshold && new_sat_count >= threshold) {
    solver->sat(c);
  } else if (old_sat_count >= threshold && new_sat_count < threshold) {
    solver->unsat(c);
  }
  solver->sat_count[c] = new_sat_count;
  if (!solver->noble_true_xor.empty() && solver->noble_eligible[c]) {
    solver->noble_true_xor[c] ^= CardSATLS::noble_literal_id(occurrence);
  }
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
    update_clause_neighbor_scores(solver, occurrence, old_sat_count,
                                  new_sat_count, maintain_goodvar_stack);
  }
  update_count_for_occurrence(solver, occurrence, old_sat_count, new_sat_count);
}

void update_flip_scores(CardSATLS *solver, int flipvar,
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
  if (!solver->weight_is_touched[variable]) {
    solver->weight_is_touched[variable] = true;
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

  const int weight = solver->unit_weight[c];
  const bool first_true_phase =
      std::min(old_sat_count, middle_sat_count) == threshold;
  const bool second_true_phase =
      std::min(middle_sat_count, final_sat_count) == threshold;
  const int first_delta = old_sat_count == threshold ? weight : -weight;
  const int second_delta = middle_sat_count == threshold ? weight : -weight;

  const lit *row = solver->clause_lit[c];
  for (int j = 0; j < solver->clause_lit_count[c]; ++j) {
    const int v = row[j].var_num;
    const bool lit_true = row[j].sense == solver->cur_soln[v];
    if (update_first && v != first_var && lit_true == first_true_phase)
      solver->score[v] += first_delta;
    if (update_second && v != second_var && lit_true == second_true_phase)
      accumulate_delayed_score(solver, v, second_delta, delayed_touched_count);
  }
}

} // namespace

void CardSATLS::flip(int v) {
  const int old_score = score[v];
  cur_soln[v] ^= 1;
  if (materialized_neighbor_mode) {
    update_flip_scores(this, v, false);
    score[v] = -old_score;
    update_goodvarstack(v);
  } else {
    update_flip_scores(this, v, true);
    score[v] = -old_score;
    if (already_in_goodvar_stack[v] != -1)
      remove_goodvar(v);
  }
}

void CardSATLS::flip_pair_atomic(int first_var, int second_var) {
  if (first_var <= 0 || second_var <= 0 || first_var > num_vars ||
      second_var > num_vars || first_var == second_var) {
    return;
  }

  // Preserve the update order for repeated occurrences and on-the-fly flips.
  if (!materialized_neighbor_mode || sat_var == nullptr ||
      has_repeated_clause_occurrence(this, first_var) ||
      has_repeated_clause_occurrence(this, second_var)) {
    flip(first_var);
    flip(second_var);
    return;
  }

  // Mark shared clauses with the polarity of the second literal.
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
    update_count_for_occurrence(this, occurrence, old_sat_count,
                                middle_sat_count);
  }
  score[first_var] = -old_first_score;
  update_goodvarstack(first_var);

  const int old_second_score = score[second_var];
  cur_soln[second_var] = 1 - cur_soln[second_var];
  for (int i = 0; i < delayed_touched_count; ++i) {
    const int v = weight_touched_vars[i];
    score[v] += weight_delta_score[v];
    weight_delta_score[v] = 0;
    weight_is_touched[v] = false;
  }

  for (int i = 0; i < var_lit_count[second_var]; ++i) {
    const lit &occurrence = var_lit[second_var][i];
    if (sat_var[occurrence.clause_num] == 0) {
      update_one_flip_occurrence(this, second_var, occurrence, false);
    } else {
      const int old_count = sat_count[occurrence.clause_num];
      const int delta = cur_soln[second_var] == occurrence.sense ? 1 : -1;
      update_count_for_occurrence(this, occurrence, old_count,
                                  old_count + delta);
    }
  }
  score[second_var] = -old_second_score;
  update_goodvarstack(second_var);

  for (int i = 0; i < shared_count; ++i) {
    sat_var[temp_array[i]] = 0;
  }
}
