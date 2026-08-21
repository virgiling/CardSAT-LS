/*
 * Binary-pattern preprocessing adapted from PRS-sc26
 * (src/preprocess/binary.cpp, commit 53ae719dce4e172e46d93f19acf4728146fc5f03).
 * Modified for native KNF constraints and recoverable signed mappings.
 * See third_party/licenses/PRS-sc26-LICENSE.
 */

#include "preprocess.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kMaximumClauses = 10000000;
constexpr int kMaximumTurns = 16;
constexpr long long kMaximumClauseRounds = 32000000LL;

std::uint64_t binary_key(int first, int second) {
  if (second < first)
    std::swap(first, second);
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(first)) << 32) |
         static_cast<std::uint32_t>(second);
}

enum class ActionKind : unsigned char { Relation, Fixed };

struct Action {
  ActionKind kind;
  int first;
  int second;
  int value;

  bool operator==(const Action &other) const {
    return kind == other.kind && first == other.first &&
           second == other.second && value == other.value;
  }
};

struct ActionHash {
  std::size_t operator()(const Action &action) const {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint32_t value) {
      for (int i = 0; i < 4; ++i) {
        hash ^= value & 0xffU;
        hash *= 1099511628211ULL;
        value >>= 8;
      }
    };
    mix(static_cast<std::uint32_t>(action.kind));
    mix(static_cast<std::uint32_t>(action.first));
    mix(static_cast<std::uint32_t>(action.second));
    mix(static_cast<std::uint32_t>(action.value));
    return static_cast<std::size_t>(hash);
  }
};

Action relation_action(int first_literal, int second_literal) {
  int first = std::abs(first_literal);
  int second = std::abs(second_literal);
  if (second < first)
    std::swap(first, second);
  return {ActionKind::Relation, first, second,
          -pnsign(first_literal) * pnsign(second_literal)};
}

Action fixed_action(int literal) {
  return {ActionKind::Fixed, std::abs(literal), 0, pnsign(literal)};
}

bool action_less(const Action &left, const Action &right) {
  if (left.kind != right.kind)
    return left.kind < right.kind;
  if (left.first != right.first)
    return left.first < right.first;
  if (left.second != right.second)
    return left.second < right.second;
  return left.value < right.value;
}

} // namespace

int Preprocessor::find(int variable) {
  int root = variable;
  int orientation = 1;
  while (f[root] != root) {
    orientation *= val[root];
    root = f[root];
  }

  // Keep path compression iterative: generated instances can contain chains
  // long enough to overflow the call stack in the recursive PRS version.
  int current = variable;
  int prefix = 1;
  while (f[current] != current) {
    const int parent = f[current];
    const int edge = val[current];
    f[current] = root;
    val[current] = orientation * prefix;
    prefix *= edge;
    current = parent;
  }
  return root;
}

bool Preprocessor::preprocess_binary() {
  if (clauses > kMaximumClauses)
    return true;

  clause_delete.setsize(clauses + 1);
  for (int id = 1; id <= clauses; ++id)
    clause_delete[id] = 0;

  in_cardinality.setsize(vars + 1);
  for (int variable = 0; variable <= vars; ++variable)
    in_cardinality[variable] = 0;
  for (int id = 1; id <= clauses; ++id) {
    if (degree[id] <= 1)
      continue;
    for (int j = 0; j < clause[id].size(); ++j)
      in_cardinality[std::abs(clause[id][j])] = 1;
  }

  for (int variable = 1; variable <= vars; ++variable) {
    f[variable] = variable;
    val[variable] = 1;
    varval[variable] = 0;
    resseen[tolit(variable)] = 0;
    resseen[tolit(-variable)] = 0;
  }
  std::vector<int> component_size(vars + 1, 1);

  auto is_safe = [&](const Action &action) {
    if (action.kind == ActionKind::Fixed)
      return !in_cardinality[action.first];
    return !in_cardinality[action.first] && !in_cardinality[action.second];
  };

  auto assign_value = [&](int variable, int wanted, bool &changed) {
    const int root = find(variable);
    const int root_value = wanted * val[variable];
    if (varval[root])
      return varval[root] == root_value;
    varval[root] = root_value;
    changed = true;
    return true;
  };

  auto unite = [&](int first, int second, int relation, bool &changed) {
    int first_root = find(first);
    int second_root = find(second);
    if (first_root == second_root)
      return val[first] == relation * val[second];

    const int edge = relation * val[first] * val[second];
    const int first_fixed = varval[first_root];
    const int second_fixed = varval[second_root];
    if (first_fixed && second_fixed && first_fixed != edge * second_fixed)
      return false;

    if (component_size[first_root] <= component_size[second_root]) {
      if (first_fixed && !second_fixed)
        varval[second_root] = edge * first_fixed;
      f[first_root] = second_root;
      val[first_root] = edge;
      varval[first_root] = 0;
      component_size[second_root] += component_size[first_root];
    } else {
      if (second_fixed && !first_fixed)
        varval[first_root] = edge * second_fixed;
      f[second_root] = first_root;
      val[second_root] = edge;
      varval[second_root] = 0;
      component_size[first_root] += component_size[second_root];
    }
    changed = true;
    return true;
  };

  bool any_changed = false;
  int turn = 0;
  while (true) {
    const long long next_clause_rounds = 1LL * (turn + 1) * clauses;
    if (turn >= kMaximumTurns || next_clause_rounds > kMaximumClauseRounds)
      break;
    ++turn;

    long long binary_rows = 0;
    for (int id = 1; id <= clauses; ++id)
      if (!clause_delete[id] && degree[id] == 1 &&
          clause[id].size() == 2)
        ++binary_rows;

    std::unordered_map<std::uint64_t, int> binary_index;
    binary_index.max_load_factor(0.8F);
    binary_index.reserve(
        static_cast<std::size_t>(binary_rows + binary_rows / 4 + 1));
    for (int id = 1; id <= clauses; ++id)
      if (!clause_delete[id] && degree[id] == 1 &&
          clause[id].size() == 2)
        binary_index.emplace(binary_key(clause[id][0], clause[id][1]), id);

    auto lookup = [&](int first, int second) {
      const auto found = binary_index.find(binary_key(first, second));
      return found == binary_index.end() ? 0 : found->second;
    };

    std::vector<unsigned char> delete_witness(clauses + 1, 0);
    std::unordered_set<Action, ActionHash> action_set;
    std::vector<Action> actions;
    auto record_action = [&](const Action &action, int first_id,
                             int second_id) {
      if (first_id == second_id || !is_safe(action))
        return;
      delete_witness[first_id] = 1;
      delete_witness[second_id] = 1;
      if (action_set.insert(action).second)
        actions.push_back(action);
    };

    // These are the same three binary patterns recognized by PRS: a signed
    // equivalence and the two possible fixed assignments. Native cardinality
    // variables are deliberately excluded by is_safe().
    for (int id = 1; id <= clauses; ++id) {
      if (clause_delete[id] || degree[id] != 1 || clause[id].size() != 2)
        continue;
      const int first = clause[id][0];
      const int second = clause[id][1];
      int witness = lookup(-first, -second);
      if (witness)
        record_action(relation_action(first, second), id, witness);
      witness = lookup(first, -second);
      if (witness)
        record_action(fixed_action(first), id, witness);
      witness = lookup(-first, second);
      if (witness)
        record_action(fixed_action(second), id, witness);
    }

    std::sort(actions.begin(), actions.end(), action_less);
    bool turn_changed = false;
    for (const Action &action : actions) {
      bool action_changed = false;
      const bool consistent =
          action.kind == ActionKind::Relation
              ? unite(action.first, action.second, action.value, action_changed)
              : assign_value(action.first, action.value, action_changed);
      if (!consistent)
        return false;
      turn_changed |= action_changed;
    }

    for (int id = 1; id <= clauses; ++id) {
      if (!delete_witness[id] || clause_delete[id])
        continue;
      clause_delete[id] = 1;
      turn_changed = true;
    }

    std::vector<int> rewritten;
    std::vector<int> marked_literals;
    for (int id = 1; id <= clauses; ++id) {
      if (clause_delete[id] || degree[id] > 1)
        continue;

      rewritten.clear();
      marked_literals.clear();
      bool satisfied = false;
      bool tautology = false;
      const int old_size = clause[id].size();
      for (int j = 0; j < old_size; ++j) {
        const int literal = clause[id][j];
        const int variable = std::abs(literal);
        const int root = find(variable);
        const int literal_sign = pnsign(literal) * val[variable];
        if (varval[root]) {
          if (varval[root] == literal_sign) {
            satisfied = true;
            break;
          }
          continue;
        }

        const int mapped_literal = literal_sign * root;
        const int code = tolit(mapped_literal);
        if (resseen[code])
          continue;
        if (resseen[negative(code)]) {
          tautology = true;
          break;
        }
        resseen[code] = 1;
        marked_literals.push_back(code);
        rewritten.push_back(mapped_literal);
      }
      for (int code : marked_literals)
        resseen[code] = 0;

      if (satisfied || tautology) {
        clause_delete[id] = 1;
        turn_changed = true;
        continue;
      }
      if (rewritten.empty())
        return false;

      bool differs = old_size != static_cast<int>(rewritten.size());
      for (int j = 0; !differs && j < old_size; ++j)
        differs = clause[id][j] != rewritten[j];
      if (!differs)
        continue;
      clause[id].clear();
      for (int literal : rewritten)
        clause[id].push(literal);
      turn_changed = true;
    }

#ifndef NDEBUG
    for (int id = 1; id <= clauses; ++id) {
      if (clause_delete[id] || degree[id] <= 1)
        continue;
      for (int j = 0; j < clause[id].size(); ++j) {
        const int variable = std::abs(clause[id][j]);
        assert(find(variable) == variable);
        assert(val[variable] == 1);
        assert(varval[variable] == 0);
      }
    }
#endif

    any_changed |= turn_changed;
    if (!turn_changed)
      break;
  }

  if (!any_changed)
    return true;

  update_var_clause_label();
  for (int original = 1; original <= orivars; ++original) {
    const int old_mapping = mapto[original];
    if (!old_mapping)
      continue;
    const int current = std::abs(old_mapping);
    const int root = find(current);
    const int orientation = pnsign(old_mapping) * val[current];
    if (varval[root]) {
      mapto[original] = 0;
      mapval[original] = orientation * varval[root];
    } else if (color[root]) {
      mapto[original] = orientation * color[root];
      mapval[original] = 0;
    } else {
      mapto[original] = 0;
      mapval[original] = orientation;
    }
  }
  return true;
}
