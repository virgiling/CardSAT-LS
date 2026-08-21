#include "cardsatLS.hpp"

#include <cassert>

using namespace std;
using namespace LocalSearch;

void CardSATLS::increase_weights() {
  int i, c, v;
  int touched_vars_count = 0;

  for (i = 0; i < hardunsat_stack_fill_pointer; ++i) {
    c = hardunsat_stack[i];
    int old_weight = unit_weight[c];
    unit_weight[c] += h_inc;
    const int delta_weight = unit_weight[c] - old_weight;
    assert(sat_count[c] < clause_true_lit_thres[c]);
    for (int j = 0; j < clause_lit_count[c]; ++j) {
      v = clause_lit[c][j].var_num;
      if (!weight_is_touched[v]) {
        weight_is_touched[v] = true;
        weight_touched_vars[touched_vars_count++] = v;
      }

      bool lit_true = clause_lit[c][j].sense == cur_soln[v];
      weight_delta_score[v] += lit_true ? -delta_weight : delta_weight;
    }
  }

  for (i = 0; i < touched_vars_count; ++i) {
    v = weight_touched_vars[i];
    int delta = weight_delta_score[v];
    weight_delta_score[v] = 0;
    weight_is_touched[v] = false;
    if (delta == 0) {
      continue;
    }
    score[v] += delta;
    if (delta > 0) {
      if (score[v] > 0 && already_in_goodvar_stack[v] == -1) {
        already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
        mypush(v, goodvar_stack);
      }
    } else if (already_in_goodvar_stack[v] != -1 && score[v] <= 0) {
      int top_v = mypop(goodvar_stack);
      goodvar_stack[already_in_goodvar_stack[v]] = top_v;
      already_in_goodvar_stack[top_v] = already_in_goodvar_stack[v];
      already_in_goodvar_stack[v] = -1;
    }
  }

  delta_total_weight += hardunsat_stack_fill_pointer;

  if (delta_total_weight >= num_clauses) {
    ave_weight += 1;
    delta_total_weight -= num_clauses;
  }
}

void CardSATLS::smooth_weights() {
  int v;

  for (int c = 0; c < num_clauses; ++c) {

    if (unit_weight[c] == 1 && sat_count[c] < clause_true_lit_thres[c]) {
      continue;
    }

    int old_weight = unit_weight[c];
    unit_weight[c] = unit_weight[c] * p_scale + scale_ave;

    for (int j = 0; j < clause_lit_count[c]; ++j) {
      v = clause_lit[c][j].var_num;
      bool lit_true = clause_lit[c][j].sense == cur_soln[v];
      int old_contribution =
          clause_score_contribution(lit_true, sat_count[c],
                                    clause_true_lit_thres[c], old_weight);
      int new_contribution =
          clause_score_contribution(lit_true, sat_count[c],
                                    clause_true_lit_thres[c], unit_weight[c]);
      int delta = new_contribution - old_contribution;
      if (delta == 0) {
        continue;
      }

      score[v] += delta;
      if (delta > 0) {
        if (score[v] > 0 && already_in_goodvar_stack[v] == -1) {
          already_in_goodvar_stack[v] = goodvar_stack_fill_pointer;
          mypush(v, goodvar_stack);
        }
      } else if (already_in_goodvar_stack[v] != -1 && score[v] <= 0) {
        int top_v = mypop(goodvar_stack);
        goodvar_stack[already_in_goodvar_stack[v]] = top_v;
        already_in_goodvar_stack[top_v] = already_in_goodvar_stack[v];
        already_in_goodvar_stack[v] = -1;
      }
    }
  }
}

void CardSATLS::update_clause_weights() {
  increase_weights();
}
