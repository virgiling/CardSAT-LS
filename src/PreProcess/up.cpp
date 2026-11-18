#include "preprocess.hpp"

bool Preprocessor::simplify_clause(int id, int &tail) {
  if (clause_delete[id])
    return true;

  int remaining = 0;
  for (int j = 0; j < clause[id].size(); ++j) {
    const int lit = clause[id][j];
    const int value = varval[abs(lit)];
    if (!value) {
      clause[id][remaining++] = lit;
    } else if (value == pnsign(lit) && --degree[id] == 0) {
      clause_delete[id] = 1;
      return true;
    }
  }
  clause[id].setsize(remaining);
  if (degree[id] != remaining)
    return degree[id] < remaining;

  clause_delete[id] = 1;
  for (int j = 0; j < remaining; ++j) {
    const int lit = clause[id][j];
    const int variable = abs(lit);
    if (varval[variable]) {
      if (varval[variable] != pnsign(lit))
        return false;
    } else {
      varval[variable] = pnsign(lit);
      q[++tail] = variable;
    }
  }
  return true;
}

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
    if (!simplify_clause(i, tail))
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
      for (const vec<int> *occurrences : {&occurp[x], &occurn[x]}) {
        for (int i = 0; i < occurrences->size(); ++i) {
          const int id = (*occurrences)[i];
          if (!clause_delete[id] && nxtc[id] != stamp) {
            nxtc[id] = stamp;
            touched.push(id);
          }
        }
      }
    }
    for (int i = 0; i < touched.size(); i++)
      if (!simplify_clause(touched[i], tail))
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
