#include "preprocess.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr int kMaximumClauses = 10000000;
constexpr int kMaximumTurns = 16;
constexpr long long kMaximumClauseRounds = 32000000LL;

// A negative representative denotes the negation of its component's root.
class BinaryRelations {
  struct Node {
    int parent;
    int sign = 1;
    int size = 1;
    int fixed = 0;
  };
  std::vector<Node> nodes;

public:
  explicit BinaryRelations(int variables) : nodes(variables + 1) {
    for (int v = 1; v <= variables; ++v)
      nodes[v].parent = v;
  }

  int representative(int variable) {
    int root = variable;
    int sign = 1;
    while (nodes[root].parent != root) {
      sign *= nodes[root].sign;
      root = nodes[root].parent;
    }

    int current = variable;
    int to_root = sign;
    while (nodes[current].parent != root) {
      const int next = nodes[current].parent;
      const int edge = nodes[current].sign;
      nodes[current].parent = root;
      nodes[current].sign = to_root;
      to_root *= edge;
      current = next;
    }
    return sign * root;
  }

  int fixed_value(int root) const { return nodes[root].fixed; }

  bool assign(int variable, int value) {
    const int rep = representative(variable);
    int &fixed = nodes[std::abs(rep)].fixed;
    const int wanted = value * pnsign(rep);
    if (fixed && fixed != wanted)
      return false;
    fixed = wanted;
    return true;
  }

  bool unite(int first, int second, int relation) {
    const int left = representative(first);
    const int right = representative(second);
    const int sign = relation * pnsign(left) * pnsign(right);
    int child = std::abs(left);
    int root = std::abs(right);
    if (child == root)
      return sign == 1;

    if (nodes[child].size > nodes[root].size)
      std::swap(child, root);
    if (nodes[child].fixed) {
      const int fixed = sign * nodes[child].fixed;
      if (nodes[root].fixed && nodes[root].fixed != fixed)
        return false;
      nodes[root].fixed = fixed;
    }
    nodes[child].parent = root;
    nodes[child].sign = sign;
    nodes[root].size += nodes[child].size;
    return true;
  }
};

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
    for (std::uint32_t value : {static_cast<std::uint32_t>(action.kind),
                                static_cast<std::uint32_t>(action.first),
                                static_cast<std::uint32_t>(action.second),
                                static_cast<std::uint32_t>(action.value)}) {
      for (int i = 0; i < 4; ++i) {
        hash ^= value & 0xffU;
        hash *= 1099511628211ULL;
        value >>= 8;
      }
    }
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

int lookup(const std::unordered_map<std::uint64_t, int> &index, int first,
           int second) {
  const auto found = index.find(binary_key(first, second));
  return found == index.end() ? 0 : found->second;
}
} // namespace

bool Preprocessor::preprocess_binary(bool &rows_changed) {
  if (clauses > kMaximumClauses)
    return true;

  clause_delete.setsize(clauses + 1);
  for (int id = 1; id <= clauses; ++id)
    clause_delete[id] = 0;

  std::vector<unsigned char> in_cardinality(vars + 1, 0);
  for (int id = 1; id <= clauses; ++id) {
    if (degree[id] <= 1)
      continue;
    for (int j = 0; j < clause[id].size(); ++j)
      in_cardinality[std::abs(clause[id][j])] = 1;
  }

  BinaryRelations relations(vars);
  std::fill_n(resseen, 2 * vars, 0);

  bool any_changed = false;
  int turn = 0;
  while (true) {
    const long long next_clause_rounds = 1LL * (turn + 1) * clauses;
    if (turn >= kMaximumTurns || next_clause_rounds > kMaximumClauseRounds)
      break;
    ++turn;

    long long binary_rows = 0;
    for (int id = 1; id <= clauses; ++id)
      if (!clause_delete[id] && degree[id] == 1 && clause[id].size() == 2)
        ++binary_rows;

    std::unordered_map<std::uint64_t, int> binary_index;
    binary_index.max_load_factor(0.8F);
    binary_index.reserve(
        static_cast<std::size_t>(binary_rows + binary_rows / 4 + 1));
    for (int id = 1; id <= clauses; ++id)
      if (!clause_delete[id] && degree[id] == 1 && clause[id].size() == 2)
        binary_index.emplace(binary_key(clause[id][0], clause[id][1]), id);

    std::vector<unsigned char> delete_witness(clauses + 1, 0);
    std::unordered_set<Action, ActionHash> action_set;
    std::vector<Action> actions;
    for (int id = 1; id <= clauses; ++id) {
      if (clause_delete[id] || degree[id] != 1 || clause[id].size() != 2)
        continue;
      const int first = clause[id][0];
      const int second = clause[id][1];
      const std::pair<int, Action> patterns[] = {
          {lookup(binary_index, -first, -second),
           relation_action(first, second)},
          {lookup(binary_index, first, -second), fixed_action(first)},
          {lookup(binary_index, -first, second), fixed_action(second)}};
      for (const auto &[witness, action] : patterns) {
        if (!witness || id == witness || in_cardinality[action.first] ||
            in_cardinality[action.second])
          continue;
        delete_witness[id] = 1;
        delete_witness[witness] = 1;
        if (action_set.insert(action).second)
          actions.push_back(action);
      }
    }

    std::sort(actions.begin(), actions.end(), action_less);
    bool turn_changed = false;
    for (const Action &action : actions) {
      const bool consistent =
          action.kind == ActionKind::Relation
              ? relations.unite(action.first, action.second, action.value)
              : relations.assign(action.first, action.value);
      if (!consistent)
        return false;
    }

    for (int id = 1; id <= clauses; ++id) {
      if (!delete_witness[id] || clause_delete[id])
        continue;
      clause_delete[id] = 1;
      turn_changed = true;
    }

    std::vector<int> rewritten;
    for (int id = 1; id <= clauses; ++id) {
      if (clause_delete[id] || degree[id] > 1)
        continue;

      rewritten.clear();
      bool satisfied = false;
      bool tautology = false;
      const int old_size = clause[id].size();
      for (int j = 0; j < old_size; ++j) {
        const int literal = clause[id][j];
        const int variable = std::abs(literal);
        const int mapped_literal =
            pnsign(literal) * relations.representative(variable);
        const int fixed = relations.fixed_value(std::abs(mapped_literal));
        if (fixed) {
          if (fixed == pnsign(mapped_literal)) {
            satisfied = true;
            break;
          }
          continue;
        }

        const int code = tolit(mapped_literal);
        if (resseen[code])
          continue;
        if (resseen[negative(code)]) {
          tautology = true;
          break;
        }
        resseen[code] = 1;
        rewritten.push_back(mapped_literal);
      }
      for (int literal : rewritten)
        resseen[tolit(literal)] = 0;

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
        assert(relations.representative(variable) == variable);
        assert(relations.fixed_value(variable) == 0);
      }
    }
#endif

    any_changed |= turn_changed;
    if (!turn_changed)
      break;
  }

  if (!any_changed)
    return true;

  rows_changed = true;
  update_var_clause_label();
  for (int original = 1; original <= orivars; ++original) {
    const int old_mapping = mapto[original];
    if (!old_mapping)
      continue;
    const int current = std::abs(old_mapping);
    const int representative = relations.representative(current);
    const int root = std::abs(representative);
    const int orientation = pnsign(old_mapping) * pnsign(representative);
    const int fixed = relations.fixed_value(root);
    if (fixed) {
      mapto[original] = 0;
      mapval[original] = orientation * fixed;
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
