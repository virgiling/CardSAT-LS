/*
 * Clique recognition adapted from PRS-sc26
 * (src/preprocess/card.cpp, commit 53ae719dce4e172e46d93f19acf4728146fc5f03).
 * Modified to perform only transactional pure-triangle compression on KNF.
 * See third_party/licenses/PRS-sc26-LICENSE.
 */

#include "preprocess.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMaximumVariables = 100000;
constexpr int kMaximumClauses = 1000000;
constexpr long long kMaximumPairProbes = 50000000LL;

struct PlannedClique {
  std::vector<int> literals;
  std::vector<int> edge_clause_ids;
  std::vector<int> duplicate_clause_ids;
};

std::uint64_t edge_key(int first, int second) {
  if (first > second)
    std::swap(first, second);
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(first)) << 32) |
         static_cast<std::uint32_t>(second);
}

bool row_matches_edge(const Preprocessor &preprocessor, int clause_id,
                      int first_vertex, int second_vertex) {
  if (clause_id < 1 || clause_id > preprocessor.clauses ||
      preprocessor.degree[clause_id] != 1 ||
      preprocessor.clause[clause_id].size() != 2)
    return false;
  const int first_literal = toeidx(first_vertex);
  const int second_literal = toeidx(second_vertex);
  const int row_first = preprocessor.clause[clause_id][0];
  const int row_second = preprocessor.clause[clause_id][1];
  return (row_first == first_literal && row_second == second_literal) ||
         (row_first == second_literal && row_second == first_literal);
}

bool verify_clique(const Preprocessor &preprocessor,
                   const PlannedClique &clique) {
  const int size = static_cast<int>(clique.literals.size());
  if (size < 3 ||
      static_cast<long long>(clique.edge_clause_ids.size()) !=
          1LL * size * (size - 1) / 2)
    return false;

  std::vector<int> variables;
  variables.reserve(size);
  for (int literal : clique.literals)
    variables.push_back(std::abs(literal));
  std::sort(variables.begin(), variables.end());
  if (std::adjacent_find(variables.begin(), variables.end()) != variables.end())
    return false;

  int edge_index = 0;
  for (int right = 1; right < size; ++right)
    for (int left = 0; left < right; ++left)
      if (!row_matches_edge(preprocessor,
                            clique.edge_clause_ids[edge_index++],
                            tolit(clique.literals[right]),
                            tolit(clique.literals[left])))
        return false;

  std::vector<int> clique_literals = clique.literals;
  std::sort(clique_literals.begin(), clique_literals.end());
  for (int clause_id : clique.duplicate_clause_ids) {
    if (clause_id < 1 || clause_id > preprocessor.clauses ||
        preprocessor.degree[clause_id] != 1 ||
        preprocessor.clause[clause_id].size() != 2)
      return false;
    const int first = preprocessor.clause[clause_id][0];
    const int second = preprocessor.clause[clause_id][1];
    if (first == second ||
        !std::binary_search(clique_literals.begin(), clique_literals.end(),
                            first) ||
        !std::binary_search(clique_literals.begin(), clique_literals.end(),
                            second))
      return false;
  }
  return true;
}

bool is_pure_triangle(const Preprocessor &preprocessor,
                      const PlannedClique &clique) {
  if (clique.literals.size() != 3 || clique.edge_clause_ids.size() != 3 ||
      !clique.duplicate_clause_ids.empty())
    return false;
  std::vector<int> edge_clause_ids = clique.edge_clause_ids;
  std::sort(edge_clause_ids.begin(), edge_clause_ids.end());
  return std::adjacent_find(edge_clause_ids.begin(), edge_clause_ids.end()) ==
             edge_clause_ids.end() &&
         verify_clique(preprocessor, clique);
}

template <typename T> void swap_storage(vec<T> &first, vec<T> &second) {
  std::swap(first.data, second.data);
  std::swap(first.sz, second.sz);
  std::swap(first.cap, second.cap);
}

void commit_rows(Preprocessor &preprocessor,
                 const std::vector<unsigned char> &delete_row,
                 std::vector<std::unique_ptr<vec<int>>> &prepared_rows,
                 const std::vector<int> &prepared_degrees) {
  int output = 0;
  const int old_clauses = preprocessor.clauses;
  for (int id = 1; id <= old_clauses; ++id) {
    if (delete_row[id])
      continue;
    ++output;
    if (output == id)
      continue;
    preprocessor.clause[output].clear(true);
    swap_storage(preprocessor.clause[output], preprocessor.clause[id]);
    preprocessor.degree[output] = preprocessor.degree[id];
  }

  assert(prepared_rows.size() == prepared_degrees.size());
  for (std::size_t index = 0; index < prepared_rows.size(); ++index) {
    ++output;
    assert(output <= old_clauses);
    preprocessor.clause[output].clear(true);
    swap_storage(preprocessor.clause[output], *prepared_rows[index]);
    preprocessor.degree[output] = prepared_degrees[index];
  }
  for (int id = output + 1; id <= old_clauses; ++id)
    preprocessor.clause[id].clear(true);
  preprocessor.clause.setsize(output + 1);
  preprocessor.degree.setsize(output + 1);
  preprocessor.clauses = output;
  preprocessor.clause_delete.setsize(output + 1);
  for (int id = 0; id <= output; ++id)
    preprocessor.clause_delete[id] = 0;
}

// This follows PRS search_almost_one, but records a transaction instead of
// mutating the formula while cliques are still being discovered.
bool search_almost_one(const Preprocessor &preprocessor,
                       std::vector<PlannedClique> &planned) {
  const int literal_vertices = preprocessor.vars * 2;
  std::vector<std::vector<int>> occurrence(literal_vertices);
  std::unordered_map<std::uint64_t, int> active_edges;
  std::unordered_map<std::uint64_t, std::vector<int>> duplicate_edge_ids;

  long long binary_rows = 0;
  for (int id = 1; id <= preprocessor.clauses; ++id)
    if (preprocessor.degree[id] == 1 &&
        preprocessor.clause[id].size() == 2)
      ++binary_rows;
  if (binary_rows < 3)
    return true;
  active_edges.max_load_factor(0.8F);
  active_edges.reserve(static_cast<std::size_t>(binary_rows));

  for (int id = 1; id <= preprocessor.clauses; ++id) {
    if (preprocessor.degree[id] != 1 ||
        preprocessor.clause[id].size() != 2)
      continue;
    const int first = tolit(preprocessor.clause[id][0]);
    const int second = tolit(preprocessor.clause[id][1]);
    if (first == second ||
        std::abs(toeidx(first)) == std::abs(toeidx(second)))
      continue;
    const std::uint64_t key = edge_key(first, second);
    const auto insertion = active_edges.emplace(key, id);
    if (!insertion.second) {
      duplicate_edge_ids[key].push_back(id);
      continue;
    }
    occurrence[first].push_back(second);
    occurrence[second].push_back(first);
  }

  for (std::vector<int> &neighbors : occurrence) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }

  long long pair_probes = 0;
  std::vector<unsigned char> seen(literal_vertices, 0);
  for (int start = 0; start < literal_vertices; ++start) {
    if (seen[start] || occurrence[start].empty())
      continue;
    seen[start] = 1;
    if (occurrence[start].size() < 2)
      continue;

    std::vector<int> neighbors;
    neighbors.reserve(occurrence[start].size());
    for (int neighbor : occurrence[start])
      if (!seen[neighbor] && occurrence[neighbor].size() >= 2)
        neighbors.push_back(neighbor);
    if (neighbors.size() < 2)
      continue;

    while (true) {
      std::vector<int> vertices{start};
      std::vector<int> edge_clause_ids;
      std::vector<int> duplicate_clause_ids;
      std::vector<std::uint64_t> consumed_keys;

      for (int candidate : neighbors) {
        bool repeated_variable = false;
        for (int member : vertices)
          if (std::abs(toeidx(member)) == std::abs(toeidx(candidate))) {
            repeated_variable = true;
            break;
          }
        if (repeated_variable)
          continue;

        std::vector<int> candidate_edge_ids;
        std::vector<std::uint64_t> candidate_keys;
        bool complete = true;
        for (int member : vertices) {
          if (++pair_probes > kMaximumPairProbes)
            return false;
          const std::uint64_t key = edge_key(candidate, member);
          const auto found = active_edges.find(key);
          if (found == active_edges.end()) {
            complete = false;
            break;
          }
          candidate_edge_ids.push_back(found->second);
          candidate_keys.push_back(key);
        }
        if (!complete)
          continue;

        edge_clause_ids.insert(edge_clause_ids.end(),
                               candidate_edge_ids.begin(),
                               candidate_edge_ids.end());
        consumed_keys.insert(consumed_keys.end(), candidate_keys.begin(),
                             candidate_keys.end());
        for (std::uint64_t key : candidate_keys) {
          const auto duplicates = duplicate_edge_ids.find(key);
          if (duplicates != duplicate_edge_ids.end())
            duplicate_clause_ids.insert(duplicate_clause_ids.end(),
                                        duplicates->second.begin(),
                                        duplicates->second.end());
        }
        vertices.push_back(candidate);
      }

      if (vertices.size() == 1)
        break;
      for (std::uint64_t key : consumed_keys)
        active_edges.erase(key);
      if (vertices.size() == 2)
        continue;

      PlannedClique clique;
      clique.edge_clause_ids = std::move(edge_clause_ids);
      clique.duplicate_clause_ids = std::move(duplicate_clause_ids);
      clique.literals.reserve(vertices.size());
      for (int vertex : vertices)
        clique.literals.push_back(toeidx(vertex));
      if (!verify_clique(preprocessor, clique))
        return false;
      planned.push_back(std::move(clique));
    }
  }
  return true;
}

} // namespace

bool Preprocessor::preprocess_card() {
  if (vars > kMaximumVariables || clauses > kMaximumClauses)
    return true;

  try {
    std::vector<PlannedClique> planned;
    if (!search_almost_one(*this, planned) || planned.empty())
      return true;

    // Larger/overlapping cliques are deliberately left unchanged. Only an
    // all-triangle transaction is currently part of the validated pipeline.
    for (const PlannedClique &clique : planned)
      if (!is_pure_triangle(*this, clique))
        return true;

    std::vector<unsigned char> delete_row(clauses + 1, 0);
    std::vector<std::unique_ptr<vec<int>>> prepared_rows;
    std::vector<int> prepared_degrees;
    int planned_maxlen = maxlen;
    prepared_rows.reserve(planned.size());
    prepared_degrees.reserve(planned.size());

    for (const PlannedClique &clique : planned) {
      if (!verify_clique(*this, clique))
        return true;
      for (int id : clique.edge_clause_ids) {
        if (delete_row[id])
          return true;
        delete_row[id] = 1;
      }
      for (int id : clique.duplicate_clause_ids) {
        if (delete_row[id])
          return true;
        delete_row[id] = 1;
      }

      auto prepared = std::make_unique<vec<int>>();
      prepared->growTo(static_cast<int>(clique.literals.size()));
      for (int j = 0; j < static_cast<int>(clique.literals.size()); ++j)
        (*prepared)[j] = clique.literals[j];
      prepared_rows.push_back(std::move(prepared));
      prepared_degrees.push_back(
          static_cast<int>(clique.literals.size()) - 1);
      planned_maxlen =
          std::max(planned_maxlen, static_cast<int>(clique.literals.size()));
    }

    commit_rows(*this, delete_row, prepared_rows, prepared_degrees);
    maxlen = planned_maxlen;
    return true;
  } catch (const OutOfMemoryException &) {
    return true;
  } catch (const std::bad_alloc &) {
    return true;
  }
}
