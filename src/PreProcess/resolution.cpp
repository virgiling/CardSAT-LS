#include "preprocess.hpp"
#include <cassert>

namespace {

class GeneratedRowsGuard {
  vec<vec<int>> &rows;

public:
  explicit GeneratedRowsGuard(vec<vec<int>> &rows) : rows(rows) {}

  void clear_rows(bool dealloc_outer = false) {
    for (int i = 0; i < rows.size(); ++i)
      rows[i].clear(true);
    rows.clear(dealloc_outer);
  }

  ~GeneratedRowsGuard() { clear_rows(true); }
};

} // namespace

bool Preprocessor::preprocess_resolution() {
  // preprocess_up() compacts clause/degree, while clause_delete still has the
  // pre-compaction length.  Keep the vectors index-aligned before appending
  // resolvents; otherwise a new clause can inherit a stale deleted flag.
  clause_delete.setsize(clauses + 1);

  vec<int> pos_count, neg_count, pure_queue, in_pure_queue;
  pos_count.growTo(vars + 1, 0);
  neg_count.growTo(vars + 1, 0);
  in_pure_queue.growTo(vars + 1, 0);

  auto enqueue_pure_candidate = [&](int v) {
    if (clean[v] || in_pure_queue[v])
      return;
    pure_queue.push(v);
    in_pure_queue[v] = 1;
  };

  auto inc_count = [&](int lit) {
    int v = abs(lit);
    if (lit > 0)
      ++pos_count[v];
    else
      ++neg_count[v];
  };

  auto dec_count = [&](int lit) {
    int v = abs(lit);
    if (lit > 0)
      --pos_count[v];
    else
      --neg_count[v];
    enqueue_pure_candidate(v);
  };

  for (int i = 1; i <= vars; i++) {
    occurn[i].clear();
    occurp[i].clear();
    resseen[i] = clean[i] = seen[i] = 0;
  }
  for (int i = 1; i <= clauses; i++) {
    clause_delete[i] = 0;
    for (int j = 0; j < clause[i].size(); j++) {
      int lit = clause[i][j];
      if (lit > 0)
        occurp[lit].push(i);
      else
        occurn[-lit].push(i);
      inc_count(lit);
    }
  }

  res_clauses = 0;
  resolutions = 0;
  res_clause.clear(true);
  res_degree.clear(true);
  resolution.clear(true);
  res_owner.clear(true);
  res_clause.push();
  res_degree.push(0);
  resolution.push();
  res_owner.push(0);

  auto store_recovery_clause = [&](int id, int owner) {
    ++res_clauses;
    res_clause.push();
    res_degree.push(degree[id]);
    res_owner.push(owner);
    for (int j = 0; j < clause[id].size(); j++) {
      int lit = clause[id][j];
      res_clause[res_clauses].push(pnsign(lit) * mapfrom[abs(lit)]);
    }
  };

  auto clear_marks = [&](vec<int> &lits) {
    for (int i = 0; i < lits.size(); i++)
      resseen[abs(lits[i])] = 0;
  };

  auto build_resolvent = [&](int pivot, int pos_id, int neg_id, vec<int> &out,
                             int &out_degree) -> int {
    out.clear();
    out_degree = degree[pos_id] + degree[neg_id] - 1;
    bool clause_resolvent = degree[pos_id] == 1 && degree[neg_id] == 1;
    for (int j = 0; j < clause[pos_id].size(); j++) {
      int lit = clause[pos_id][j];
      if (abs(lit) == pivot)
        continue;
      if (clause_resolvent && resseen[abs(lit)] == pnsign(lit))
        continue;
      if (clause_resolvent && resseen[abs(lit)] == -pnsign(lit)) {
        clear_marks(out);
        out.clear();
        return 2;
      }
      if (!clause_resolvent && resseen[abs(lit)]) {
        clear_marks(out);
        out.clear();
        return 0;
      }
      resseen[abs(lit)] = pnsign(lit);
      out.push(lit);
    }
    for (int j = 0; j < clause[neg_id].size(); j++) {
      int lit = clause[neg_id][j];
      if (abs(lit) == pivot)
        continue;
      if (clause_resolvent && resseen[abs(lit)] == pnsign(lit))
        continue;
      if (clause_resolvent && resseen[abs(lit)] == -pnsign(lit)) {
        clear_marks(out);
        out.clear();
        return 2;
      }
      if (!clause_resolvent && resseen[abs(lit)]) {
        clear_marks(out);
        out.clear();
        return 0;
      }
      resseen[abs(lit)] = pnsign(lit);
      out.push(lit);
    }
    clear_marks(out);
    if (out_degree <= 0)
      return 2;
    if (out_degree > out.size())
      return -1;
    return 1;
  };

  auto append_clause = [&](vec<int> &lits, int deg) {
    ++clauses;
    clause.push();
    degree.push(deg);
    assert(clause_delete.size() == clauses);
    clause_delete.push(0);
    for (int i = 0; i < lits.size(); i++) {
      int lit = lits[i];
      clause[clauses].push(lit);
      if (lit > 0)
        occurp[lit].push(clauses);
      else
        occurn[-lit].push(clauses);
      inc_count(lit);
    }
  };

  auto delete_active_clause = [&](int id) {
    if (clause_delete[id])
      return;
    for (int i = 0; i < clause[id].size(); i++)
      dec_count(clause[id][i]);
    clause_delete[id] = 1;
  };

  int r = 0;
  bool changed = false;
  vec<int> pos, neg, lits, generated_degree;
  vec<vec<int>> generated;
  GeneratedRowsGuard generated_guard(generated);
  for (int v = 1; v <= vars; v++) {
    pos.clear();
    neg.clear();
    for (int j = 0; j < occurp[v].size(); j++)
      if (!clause_delete[occurp[v][j]])
        pos.push(occurp[v][j]);
    for (int j = 0; j < occurn[v].size(); j++)
      if (!clause_delete[occurn[v][j]])
        neg.push(occurn[v][j]);
    if (!pos_count[v] || !neg_count[v] ||
        1ll * pos_count[v] * neg_count[v] > pos_count[v] + neg_count[v])
      continue;

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
    bool positive_residuals_implied =
        all_pos_clauses || has_tight_neg_tail;
    bool negative_residuals_implied =
        all_neg_clauses || has_tight_pos_tail;
    if (!positive_residuals_implied || !negative_residuals_implied)
      continue;

    generated_guard.clear_rows();
    generated_degree.clear();
    bool can_eliminate = true;
    for (int i = 0; can_eliminate && i < pos.size(); i++) {
      for (int j = 0; j < neg.size(); j++) {
        int deg = 0;
        int res = build_resolvent(v, pos[i], neg[j], lits, deg);
        if (res < 0) {
          generated_guard.clear_rows();
          return false;
        }
        if (res == 0) {
          can_eliminate = false;
          break;
        }
        if (res == 1) {
          generated.push();
          lits.copyTo(generated.last());
          generated_degree.push(deg);
        }
      }
    }
    if (!can_eliminate) {
      generated_guard.clear_rows();
      continue;
    }

    for (int i = 0; i < pos.size(); i++) {
      store_recovery_clause(pos[i], mapfrom[v]);
      delete_active_clause(pos[i]);
    }
    for (int i = 0; i < neg.size(); i++) {
      store_recovery_clause(neg[i], mapfrom[v]);
      delete_active_clause(neg[i]);
    }
    for (int i = 0; i < generated.size(); i++)
      append_clause(generated[i], generated_degree[i]);
    generated_guard.clear_rows();
    generated_degree.clear();
    q[++r] = v;
    clean[v] = 1;
    changed = true;
  }
  generated_guard.clear_rows(true);

  auto simplify_pure_clause = [&](int id, int v, int val) -> bool {
    if (clause_delete[id])
      return true;
    int t = 0;
    for (int j = 0; j < clause[id].size(); j++) {
      int lit = clause[id][j];
      if (abs(lit) != v) {
        clause[id][t++] = lit;
        continue;
      }
      dec_count(lit);
      if (val == pnsign(lit)) {
        --degree[id];
      }
    }
    clause[id].setsize(t);
    if (degree[id] == 0) {
      for (int j = 0; j < t; j++)
        dec_count(clause[id][j]);
      clause_delete[id] = 1;
      clause[id].setsize(0);
      return true;
    }
    return degree[id] <= t;
  };

  for (int v = 1; v <= vars; v++)
    if (!clean[v] && (!pos_count[v] || !neg_count[v]))
      enqueue_pure_candidate(v);

  int pure_head = 0;
  while (pure_head < pure_queue.size()) {
    int v = pure_queue[pure_head++];
    in_pure_queue[v] = 0;
    if (clean[v] || (pos_count[v] && neg_count[v]))
      continue;
    if (!pos_count[v] && !neg_count[v]) {
      clean[v] = 1;
      continue;
    }

    int orig = mapfrom[v];
    int val = pos_count[v] ? 1 : -1;
    mapval[orig] = val;
    mapto[orig] = 0;
    clean[v] = 1;
    changed = true;
    vec<int> &occ = val > 0 ? occurp[v] : occurn[v];
    for (int j = 0; j < occ.size(); j++)
      if (!simplify_pure_clause(occ[j], v, val))
        return false;
  }

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
