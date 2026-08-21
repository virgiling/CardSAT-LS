#include "preprocess.hpp"
#include "parse.hpp"
#include <cstdlib>

namespace {

void clear_nested_rows(vec<vec<int>> &rows) {
  for (int i = 0; i < rows.size(); ++i)
    rows[i].clear(true);
  rows.clear(true);
}

} // namespace

Preprocessor::Preprocessor()
    : vars(0), clauses(0), maxlen(0), orivars(0), oriclauses(0), res_clauses(0),
      resolutions(0) {}

Preprocessor::Preprocessor(const char *filename) : Preprocessor() {
  read_file(filename);
}

Preprocessor::~Preprocessor() { clear_problem(); }

void Preprocessor::clear_problem() {
  release();
  delete[] mapto;
  delete[] mapval;
  mapto = nullptr;
  mapval = nullptr;

  clear_nested_rows(clause);
  clear_nested_rows(res_clause);
  degree.clear(true);
  res_degree.clear(true);
  res_owner.clear(true);
  resolution.clear(true);

  vars = 0;
  clauses = 0;
  maxlen = 0;
  orivars = 0;
  oriclauses = 0;
  res_clauses = 0;
  resolutions = 0;
}

void Preprocessor::read_file(const char *filename) {
  clear_problem();
  readfile(filename, &vars, &clauses, clause, degree);
  orivars = vars;
  oriclauses = clauses;
  for (int i = 1; i <= clauses; i++) {
    if (degree[i] == 0) {
      degree[i] = 1;
    }
    int l = clause[i].size();
    if (l > maxlen)
      maxlen = l;
  }
}

void Preprocessor::preprocess_init() {
  release();
  delete[] mapto;
  delete[] mapval;
  mapto = nullptr;
  mapval = nullptr;

  f = new int[vars + 10];
  val = new int[vars + 10];
  color = new int[vars + 10];
  varval = new int[vars + 10];
  q = new int[vars + 10];
  clean = new int[vars + 10];
  seen = new int[(vars << 1) + 10];
  clause_delete.growTo(clauses + 1, 0);
  nxtc.growTo(clauses + 1, 0);
  occurp = new vec<int>[vars + 1];
  occurn = new vec<int>[vars + 1];
  in_cardinality.growTo(vars + 1, 0);
  for (int i = 1; i <= clauses; i++) {
    if (degree[i] > 1) {
      for (int j = 0; j < clause[i].size(); j++) {
        in_cardinality[abs(clause[i][j])] = 1;
      }
    }
    if (degree[i] == 0) {
      degree[i] = 1;
    }
    int l = clause[i].size();
    if (l > maxlen)
      maxlen = l;
  }
  resseen = new int[(vars << 1) + 10];

  mapval = new int[vars + 10];
  mapto = new int[vars + 10];
  for (int i = 1; i <= vars; i++)
    mapto[i] = i, mapval[i] = 0;
}

void Preprocessor::release() {
  delete[] f;
  f = nullptr;
  delete[] val;
  val = nullptr;
  delete[] color;
  color = nullptr;
  delete[] varval;
  varval = nullptr;
  delete[] q;
  q = nullptr;
  delete[] clean;
  clean = nullptr;
  delete[] seen;
  seen = nullptr;
  clause_delete.clear(true);
  nxtc.clear(true);
  delete[] resseen;
  resseen = nullptr;
  delete[] mapfrom;
  mapfrom = nullptr;
  delete[] occurp;
  occurp = nullptr;
  delete[] occurn;
  occurn = nullptr;
  in_cardinality.clear(true);
}

void Preprocessor::update_var_clause_label() {
  int remain_var = 0;
  for (int i = 1; i <= vars; i++)
    color[i] = 0;
  for (int i = 1; i <= clauses; i++) {
    if (clause_delete[i]) {
      continue;
    }
    int l = clause[i].size();
    for (int j = 0; j < l; j++) {
      int v = abs(clause[i][j]);
      color[v] = 1;
    }
  }
  for (int i = 1; i <= vars; i++) {
    if (color[i] == 1)
      color[i] = ++remain_var;
  }

  int id = 0;
  for (int i = 1; i <= clauses; i++) {
    if (clause_delete[i]) {
      clause[i].setsize(0);
      continue;
    }
    ++id;
    int l = clause[i].size();
    if (i == id) {
      for (int j = 0; j < l; j++)
        clause[id][j] = color[abs(clause[i][j])] * pnsign(clause[i][j]);
      degree[id] = degree[i];
      continue;
    }
    clause[id].setsize(0);
    for (int j = 0; j < l; j++)
      clause[id].push(color[abs(clause[i][j])] * pnsign(clause[i][j]));
    degree[id] = degree[i];
  }
  for (int i = id + 1; i <= clauses; i++)
    clause[i].clear(true);
  for (int i = remain_var + 1; i <= vars; i++)
    occurp[i].clear(true), occurn[i].clear(true);
  clause.setsize(id + 1);
  degree.setsize(id + 1);
  vars = remain_var, clauses = id;
}

bool Preprocessor::get_complete_model() {
  int r = 0;
  for (int i = 1; i <= orivars; i++)
    if (!mapto[i]) {
      if (!mapval[i])
        ;
      else if (abs(mapval[i]) != 1)
        mapval[i] = 0, ++r;
    }
  if (r) {
    if (res_owner.size() != res_clauses + 1 ||
        resolution.size() != resolutions + 1)
      return false;
    auto satisfied_with = [&](int cid, int pivot, int val) -> bool {
      int true_lits = 0;
      for (int j = 0; j < res_clause[cid].size(); j++) {
        int lit = res_clause[cid][j], v = abs(lit);
        int cur = (v == pivot ? val : mapval[v]);
        if (cur == pnsign(lit))
          ++true_lits;
      }
      return true_lits >= res_degree[cid];
    };
    auto clauses_with_pivot_satisfied = [&](int pivot, int val, int first,
                                            int last) -> bool {
      for (int cid = first; cid <= last; ++cid)
        if (!satisfied_with(cid, pivot, val))
          return false;
      return true;
    };
    int last_owned_clause = res_clauses;
    for (int ii = resolutions; ii >= 1; ii--) {
      int v = resolution[ii];
      int group_last = last_owned_clause;
      while (last_owned_clause >= 1 && res_owner[last_owned_clause] == v)
        --last_owned_clause;
      int group_first = last_owned_clause + 1;
      if (group_first > group_last)
        return false;
      if (clauses_with_pivot_satisfied(v, 1, group_first, group_last)) {
        mapval[v] = 1;
        continue;
      }
      if (clauses_with_pivot_satisfied(v, -1, group_first, group_last)) {
        mapval[v] = -1;
        continue;
      }
      return false;
    }
    if (last_owned_clause != 0)
      return false;
    clear_nested_rows(res_clause);
    res_degree.clear(true);
    resolution.clear(true);
    res_owner.clear(true);
  }
  return true;
}

int Preprocessor::do_preprocess(const char *filename) {
  read_file(filename);
  preprocess_init();

  auto fail = [&]() {
    clear_problem();
    return 20;
  };

  if (!preprocess_up())
    return fail();

  if (!preprocess_resolution())
    return fail();

  // if (!preprocess_card())
  //   return fail();

  if (!preprocess_binary())
    return fail();

  printf("c after preprocessing, vars = %d, clauses = %d\n", vars, clauses);

  release();
  return 0;
}

void Preprocessor::write_to_file(const char *filename) {
  std::ofstream fout(filename);
  fout << "p knf " << vars << " " << clauses << std::endl;
  for (int i = 1; i <= clauses; i++) {
    if (degree[i] > 1) {
      fout << "k " << degree[i] << " ";
    }
    for (int j = 0; j < clause[i].size(); j++) {
      fout << clause[i][j] << " ";
    }
    fout << "0" << std::endl;
  }
  fout.close();
}
