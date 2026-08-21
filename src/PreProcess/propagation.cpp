#include "preprocess.hpp"

bool Preprocessor::preprocess_up() {
  for (int i = 1; i <= vars; i++) {
    varval[i] = 0;
    occurp[i].clear();
    occurn[i].clear();
    seen[i] = 0;
    resseen[(i - 1) << 1] = resseen[(i - 1) << 1 | 1] = 0;
  }
  for (int i = 1; i <= clauses; i++)
    clause_delete[i] = 0;
  int head = 1, tail = 0;

  auto assign_lit = [&](int lit) -> bool {
    int v = abs(lit), val = pnsign(lit);
    if (varval[v])
      return varval[v] == val;
    varval[v] = val;
    q[++tail] = v;
    return true;
  };

  auto force_remaining = [&](int id) -> bool {
    clause_delete[id] = 1;
    for (int j = 0; j < clause[id].size(); j++)
      if (!assign_lit(clause[id][j]))
        return false;
    return true;
  };

  auto simplify_clause = [&](int id) -> bool {
    if (clause_delete[id])
      return true;
    int t = 0;
    for (int j = 0; j < clause[id].size(); j++) {
      int lit = clause[id][j], v = abs(lit);
      if (!varval[v]) {
        clause[id][t++] = lit;
      } else if (varval[v] == pnsign(lit)) {
        --degree[id];
        if (degree[id] == 0) {
          clause_delete[id] = 1;
          clause[id].setsize(0);
          return true;
        }
      }
    }
    clause[id].setsize(t);
    if (degree[id] > t)
      return false;
    if (degree[id] == t)
      return force_remaining(id);
    return true;
  };

  for (int i = 1; i <= clauses; i++) {
    int l = clause[i].size(), t = 0;
    for (int j = 0; j < l; j++) {
      int lit = tolit(clause[i][j]), v = abs(clause[i][j]);
      if (resseen[lit] == i)
        continue;
      if (resseen[negative(lit)] == i) {
        if (seen[v] != i) {
          seen[v] = i;
          --degree[i];
          if (degree[i] == 0) {
            clause_delete[i] = 1;
            break;
          }
        }
      }
      resseen[lit] = i;
      clause[i][t++] = clause[i][j];
    }
    if (clause_delete[i])
      continue;

    int nt = 0;
    for (int j = 0; j < t; j++)
      if (seen[abs(clause[i][j])] != i)
        clause[i][nt++] = clause[i][j];
    clause[i].setsize(nt);

    for (int j = 0; j < nt; j++) {
      if (clause[i][j] > 0)
        occurp[clause[i][j]].push(i);
      else
        occurn[-clause[i][j]].push(i);
    }
    if (!simplify_clause(i))
      return false;
  }

  vec<int> touched;
  int stamp = 0;
  while (head <= tail) {
    int old_tail = tail;
    touched.clear();
    ++stamp;
    for (; head <= old_tail; head++) {
      int x = q[head];
      for (int i = 0; i < occurp[x].size(); i++) {
        int o = occurp[x][i];
        if (!clause_delete[o] && nxtc[o] != stamp) {
          nxtc[o] = stamp;
          touched.push(o);
        }
      }
      for (int i = 0; i < occurn[x].size(); i++) {
        int o = occurn[x][i];
        if (!clause_delete[o] && nxtc[o] != stamp) {
          nxtc[o] = stamp;
          touched.push(o);
        }
      }
    }
    for (int i = 0; i < touched.size(); i++)
      if (!simplify_clause(touched[i]))
        return false;
  }
  update_var_clause_label();
  for (int i = 1; i <= tail; i++) {
    int v = q[i];
    mapval[v] = varval[v];
  }
  mapfrom = new int[vars + 1];
  for (int i = 1; i <= vars; i++)
    mapfrom[i] = 0;
  for (int i = 1; i <= orivars; i++) {
    if (color[i])
      mapto[i] = color[i], mapfrom[color[i]] = i;
    else if (!mapval[i]) // not in unit queue, then it is no use var
      mapto[i] = 0, mapval[i] = 1;
    else
      mapto[i] = 0;
  }
  return true;
}
