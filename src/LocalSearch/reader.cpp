#include "cardsatLS.hpp"
#include <new>

using namespace std;
using namespace LocalSearch;

void CardSATLS::parse_from_preprocess(Preprocessor *preprocessor) {
  num_vars = preprocessor->vars;
  num_clauses = preprocessor->clauses;
  allocate_memory();

  temp_var *temp_weight = new temp_var[num_vars];
  int cur_weight;
  int cur_lit;

  int c = 0, i, j, v;
  for (i = 1; i <= preprocessor->clauses; ++i) {
    int l = preprocessor->clause[i].size();
    clause_lit_count[c] = l;
    clause_true_lit_thres[c] = preprocessor->degree[i];
    clause_lit[c] = new lit[l + 1];

    for (j = 0; j < l; ++j) {
      clause_lit[c][j].clause_num = c;
      clause_lit[c][j].var_num = abs(preprocessor->clause[i][j]);
      clause_lit[c][j].sense = preprocessor->clause[i][j] > 0 ? 1 : 0;
      var_lit_count[clause_lit[c][j].var_num]++;
    }
    max_clause_length = max(max_clause_length, l);
    clause_lit[c][l].var_num = 0;
    clause_lit[c][l].clause_num = -1;
    c++;
  }

  // creat var literal arrays
  long long tmp_lit_num = 0;
  for (v = 1; v <= num_vars; ++v) {
    var_lit[v] = new lit[var_lit_count[v] + 1];
    tmp_lit_num += var_lit_count[v];
    var_lit_count[v] = 0;
  }
  avg_neighbor_lit =
      double(tmp_lit_num - num_clauses) / (num_vars - num_clauses + 1);
  // cout << "c avg_neighbor_lit: " << avg_neighbor_lit<< endl;

  // scan all clauses to build up var literal arrays
  for (c = 0; c < num_clauses; ++c) {
    for (i = 0; i < clause_lit_count[c]; ++i) {
      v = clause_lit[c][i].var_num;
      var_lit[v][var_lit_count[v]++] = clause_lit[c][i];
    }
  }
}

bool CardSATLS::build_neighbor_relation(uint64_t entry_budget) {
  // cout << "c start build neighbor" << endl;
  int i, j, count;
  int v, c, n;
  int temp_neighbor_count;
  uint64_t total_neighbor_entries = 0;

  auto clear_materialized_neighbors = [&](int end_var) {
    for (int u = 1; u < end_var; ++u) {
      delete[] var_neighbor[u];
      var_neighbor[u] = nullptr;
      var_neighbor_count[u] = 0;
    }
  };

  for (v = 1; v <= num_vars; ++v) {
    neighbor_flag[v] = 1;
    temp_neighbor_count = 0;

    for (i = 0; i < var_lit_count[v]; ++i) {
      c = var_lit[v][i].clause_num;
      for (j = 0; j < clause_lit_count[c]; ++j) {
        n = clause_lit[c][j].var_num;
        if (neighbor_flag[n] != 1) {
          neighbor_flag[n] = 1;
          temp_neighbor[temp_neighbor_count++] = n;
        }
      }
    }

    neighbor_flag[v] = 0;
    const uint64_t new_entries = static_cast<uint64_t>(temp_neighbor_count);
    if (new_entries > entry_budget - total_neighbor_entries) {
      for (i = 0; i < temp_neighbor_count; ++i) {
        neighbor_flag[temp_neighbor[i]] = 0;
      }
      clear_materialized_neighbors(v);
      return false;
    }

    try {
      var_neighbor[v] = new int[temp_neighbor_count];
    } catch (const std::bad_alloc &) {
      for (i = 0; i < temp_neighbor_count; ++i) {
        neighbor_flag[temp_neighbor[i]] = 0;
      }
      clear_materialized_neighbors(v);
      return false;
    }
    total_neighbor_entries += new_entries;
    var_neighbor_count[v] = temp_neighbor_count;

    count = 0;
    for (i = 0; i < temp_neighbor_count; i++) {
      var_neighbor[v][count++] = temp_neighbor[i];
      neighbor_flag[temp_neighbor[i]] = 0;
    }
  }
  // cout << "c end build neighbor" << endl;
  return true;
}

void CardSATLS::write_instance() {
  cout << "p knf " << num_vars << " " << num_clauses << endl;
  for (int c = 0; c < num_clauses; c++) {
    if (clause_true_lit_thres[c] > 1) {
      // Card
      cout << "k " << clause_true_lit_thres[c] << " ";
      for (int i = 0; i < clause_lit_count[c]; i++) {
        int sense = clause_lit[c][i].sense;
        if (sense == 1) {
          cout << clause_lit[c][i].var_num << " ";
        } else {
          cout << -clause_lit[c][i].var_num << " ";
        }
      }
      cout << "0" << endl;
    } else {
      // CNF
      for (int i = 0; i < clause_lit_count[c]; i++) {
        int sense = clause_lit[c][i].sense;
        if (sense == 1) {
          cout << clause_lit[c][i].var_num << " ";
        } else {
          cout << -clause_lit[c][i].var_num << " ";
        }
      }
      cout << "0" << endl;
    }
  }
}
