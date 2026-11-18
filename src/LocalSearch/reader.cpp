#include "cardsatLS.hpp"
#include "preprocess.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace LocalSearch;

bool CardSATLS::parse_from_preprocess(const Preprocessor &input) {
  num_vars = input.vars;
  num_clauses = input.clauses;
  size_t literals = 0;
  for (int id = 1; id <= num_clauses; ++id) {
    const int length = input.clause[id].size();
    if (input.degree[id] > length)
      return false;
    literals += length;
    max_clause_length = std::max(max_clause_length, length);
  }

  allocate_memory();
  literal_storage = new lit[2 * literals];
  lit *row = literal_storage;
  for (int c = 0; c < num_clauses; ++c) {
    const auto &source = input.clause[c + 1];
    clause_lit[c] = row;
    clause_lit_count[c] = source.size();
    clause_true_lit_thres[c] = input.degree[c + 1];
    for (int j = 0; j < source.size(); ++j) {
      const int value = source[j];
      const int v = std::abs(value);
      *row++ = {c, v, value > 0};
      ++var_lit_count[v];
    }
  }

  lit *next = row;
  for (int v = 1; v <= num_vars; ++v) {
    var_lit[v] = next;
    next += var_lit_count[v];
    var_lit_count[v] = 0;
  }
  for (const lit *p = literal_storage; p != row; ++p) {
    const int v = p->var_num;
    var_lit[v][var_lit_count[v]++] = *p;
  }
  avg_neighbor_lit = (static_cast<double>(literals) - num_clauses) /
                     (num_vars - num_clauses + 1.0);
  return true;
}

bool CardSATLS::build_neighbor_relation(uint64_t entry_budget) {
  // Use the weight work buffers before the first search step. The variable ID
  // marks this pass, so a neighbor only enters the list on its first occurrence.
  int *seen = weight_delta_score;
  int *neighbors = weight_touched_vars;
  for (int v = 1; v <= num_vars; ++v) {
    seen[v] = v;
    int count = 0;
    for (int i = 0; i < var_lit_count[v]; ++i) {
      const int c = var_lit[v][i].clause_num;
      for (int j = 0; j < clause_lit_count[c]; ++j) {
        const int u = clause_lit[c][j].var_num;
        if (seen[u] != v) {
          seen[u] = v;
          neighbors[count++] = u;
        }
      }
    }

    if (count == 0)
      continue;
    if (static_cast<uint64_t>(count) <= entry_budget)
      var_neighbor[v] = new (std::nothrow) int[count];
    if (!var_neighbor[v]) {
      for (int u = 1; u < v; ++u) {
        delete[] var_neighbor[u];
        var_neighbor[u] = nullptr;
        var_neighbor_count[u] = 0;
      }
      return false;
    }
    std::copy_n(neighbors, count, var_neighbor[v]);
    var_neighbor_count[v] = count;
    entry_budget -= count;
  }
  return true;
}

void CardSATLS::prepare_neighbors(uint64_t entry_budget) {
  const uint64_t length = max_clause_length;
  const uint64_t largest_row_entries = length > 1 ? length * (length - 1) : 0;
  materialized_neighbor_mode = false;
  if (avg_neighbor_lit < 1e7 && largest_row_entries <= entry_budget) {
    materialized_neighbor_mode = build_neighbor_relation(entry_budget);
    std::fill_n(weight_delta_score + 1, num_vars, 0);
  }

  // Candidate selection stores up to one row. Pair flips in materialized mode
  // also use this buffer for the IDs of clauses containing both variables.
  int scratch_size = max_clause_length;
  if (materialized_neighbor_mode) {
    sat_var = new int[num_clauses]();
    scratch_size = std::max(scratch_size, num_clauses);
  }
  temp_array = new int[scratch_size];
  std::printf("c neighbor_mode %s\n",
              materialized_neighbor_mode ? "materialized" : "on_the_fly");
}
