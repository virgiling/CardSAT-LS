#include "preprocess.hpp"
#include <cassert>
#include <vector>

namespace {
struct Resolvent {
  std::vector<int> literals;
  int degree;
};

enum class ResolveResult { Keep, Redundant, Overlap, Conflict };

ResolveResult build_resolvent(const Preprocessor &p, int pivot, int positive,
                              int negative, int *marks, Resolvent &row) {
  row.degree = p.degree[positive] + p.degree[negative] - 1;
  const bool both_cnf = p.degree[positive] == 1 && p.degree[negative] == 1;
  auto result = ResolveResult::Keep;
  for (int id : {positive, negative}) {
    for (int j = 0; j < p.clause[id].size(); ++j) {
      const int lit = p.clause[id][j];
      const int variable = abs(lit);
      if (variable == pivot)
        continue;
      if (marks[variable]) {
        if (both_cnf && marks[variable] == pnsign(lit))
          continue;
        result = both_cnf ? ResolveResult::Redundant : ResolveResult::Overlap;
        break;
      }
      marks[variable] = pnsign(lit);
      row.literals.push_back(lit);
    }
    if (result != ResolveResult::Keep)
      break;
  }
  for (int lit : row.literals)
    marks[abs(lit)] = 0;
  if (result != ResolveResult::Keep)
    return result;
  if (row.degree <= 0)
    return ResolveResult::Redundant;
  if (row.degree > static_cast<int>(row.literals.size()))
    return ResolveResult::Conflict;
  return ResolveResult::Keep;
}
} // namespace

void Preprocessor::remove_resolution_parent(int id, int owner,
                                            PureLiteralQueue &candidates) {
  ++res_clauses;
  res_clause.push();
  res_degree.push(degree[id]);
  res_owner.push(owner);
  for (int j = 0; j < clause[id].size(); ++j) {
    const int lit = clause[id][j];
    res_clause[res_clauses].push(pnsign(lit) * mapfrom[abs(lit)]);
    candidates.remove_literal(lit);
  }
  clause_delete[id] = 1;
}

bool Preprocessor::preprocess_resolution(bool &changed) {
  changed = false;
  clause_delete.setsize(clauses + 1);
  PureLiteralQueue candidates(vars);
  for (int i = 1; i <= vars; ++i) {
    occurn[i].clear();
    occurp[i].clear();
    resseen[i] = 0;
  }
  for (int i = 1; i <= clauses; ++i) {
    clause_delete[i] = 0;
    for (int j = 0; j < clause[i].size(); ++j) {
      const int lit = clause[i][j];
      (lit > 0 ? occurp[lit] : occurn[-lit]).push(i);
      ++candidates.count(lit);
    }
  }

  res_clause.push();
  res_degree.push(0);
  resolution.push(0);
  res_owner.push(0);

  int r = 0;
  vec<int> pos, neg;
  std::vector<Resolvent> generated;
  for (int v = 1; v <= vars; v++) {
    const int positives = candidates.positive[v];
    const int negatives = candidates.negative[v];
    if (!positives || !negatives ||
        1LL * positives * negatives > 1LL * positives + negatives)
      continue;
    pos.clear();
    neg.clear();
    for (int j = 0; j < occurp[v].size(); j++)
      if (!clause_delete[occurp[v][j]])
        pos.push(occurp[v][j]);
    for (int j = 0; j < occurn[v].size(); j++)
      if (!clause_delete[occurn[v][j]])
        neg.push(occurn[v][j]);
    bool all_pos_clauses = true, all_neg_clauses = true;
    bool has_tight_pos_tail = false, has_tight_neg_tail = false;
    for (int i = 0; i < pos.size(); ++i) {
      all_pos_clauses &= degree[pos[i]] == 1;
      has_tight_pos_tail |= degree[pos[i]] >= clause[pos[i]].size() - 1;
    }
    for (int i = 0; i < neg.size(); ++i) {
      all_neg_clauses &= degree[neg[i]] == 1;
      has_tight_neg_tail |= degree[neg[i]] >= clause[neg[i]].size() - 1;
    }
    // A clause parent contributes every positive residual and a tight
    // cardinality parent implies the missing residuals from the opposite
    // side. Both directions are required before parent rows may be removed.
    bool positive_residuals_implied = all_pos_clauses || has_tight_neg_tail;
    bool negative_residuals_implied = all_neg_clauses || has_tight_pos_tail;
    if (!positive_residuals_implied || !negative_residuals_implied)
      continue;

    generated.clear();
    bool can_eliminate = true;
    for (int i = 0; can_eliminate && i < pos.size(); ++i) {
      for (int j = 0; j < neg.size(); ++j) {
        Resolvent &row = generated.emplace_back();
        const auto status =
            build_resolvent(*this, v, pos[i], neg[j], resseen, row);
        if (status == ResolveResult::Conflict)
          return false;
        if (status == ResolveResult::Overlap) {
          can_eliminate = false;
          break;
        }
        if (status == ResolveResult::Redundant)
          generated.pop_back();
      }
    }
    if (!can_eliminate)
      continue;

    for (int i = 0; i < pos.size(); ++i)
      remove_resolution_parent(pos[i], mapfrom[v], candidates);
    for (int i = 0; i < neg.size(); ++i)
      remove_resolution_parent(neg[i], mapfrom[v], candidates);

    for (const Resolvent &row : generated) {
      ++clauses;
      clause.push();
      degree.push(row.degree);
      assert(clause_delete.size() == clauses);
      clause_delete.push(0);
      clause[clauses].capacity(static_cast<int>(row.literals.size()));
      for (int lit : row.literals) {
        clause[clauses].push_(lit);
        (lit > 0 ? occurp[lit] : occurn[-lit]).push(clauses);
        ++candidates.count(lit);
      }
    }
    q[++r] = v;
    candidates.eliminated[v] = 1;
    changed = true;
  }

  if (!simplify_pure_literals(candidates, changed))
    return false;

  if (!changed)
    return true;

  resolutions = r;
  for (int i = 1; i <= r; i++) {
    int v = mapfrom[q[i]];
    resolution.push(v);
    mapto[v] = 0;
    mapval[v] = -10;
  }

  update_var_clause_label();
  for (int i = 1; i <= orivars; i++) {
    if (mapto[i]) {
      mapto[i] = color[mapto[i]];
      if (!mapto[i] && !mapval[i])
        mapval[i] = 1;
    } else if (!mapval[i]) {
      mapval[i] = 1;
    }
  }
  return true;
}

bool Preprocessor::recovery_clauses_satisfied(int first, int last, int pivot,
                                              int value) const {
  for (int id = first; id <= last; ++id) {
    int satisfied = 0;
    for (int j = 0; j < res_clause[id].size(); ++j) {
      const int lit = res_clause[id][j];
      const int variable = abs(lit);
      const int assignment = variable == pivot ? value : mapval[variable];
      if (assignment == pnsign(lit) && ++satisfied >= res_degree[id])
        break;
    }
    if (satisfied < res_degree[id])
      return false;
  }
  return true;
}

bool Preprocessor::get_complete_model() {
  bool needs_recovery = false;
  for (int i = 1; i <= orivars; ++i) {
    if (!mapto[i] && abs(mapval[i]) > 1) {
      mapval[i] = 0;
      needs_recovery = true;
    }
  }
  if (needs_recovery) {
    if (res_owner.size() != res_clauses + 1 ||
        resolution.size() != resolutions + 1)
      return false;
    int last_owned_clause = res_clauses;
    for (int ii = resolutions; ii >= 1; ii--) {
      int v = resolution[ii];
      int group_last = last_owned_clause;
      while (last_owned_clause >= 1 && res_owner[last_owned_clause] == v)
        --last_owned_clause;
      int group_first = last_owned_clause + 1;
      if (group_first > group_last)
        return false;
      if (recovery_clauses_satisfied(group_first, group_last, v, 1)) {
        mapval[v] = 1;
        continue;
      }
      if (recovery_clauses_satisfied(group_first, group_last, v, -1)) {
        mapval[v] = -1;
        continue;
      }
      return false;
    }
    if (last_owned_clause != 0)
      return false;
    clear_recovery_rows();
  }
  return true;
}
