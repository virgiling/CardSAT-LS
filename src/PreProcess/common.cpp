#include "preprocess.hpp"
#include "option.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

const char *read_whitespace(const char *p) {
  while ((*p >= 9 && *p <= 13) || *p == 32)
    ++p;
  return p;
}

int read_int(const char *&p) {
  p = read_whitespace(p);
  const bool negative = *p == '-';
  if (negative)
    ++p;
  int value = 0;
  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (*p++ - '0');
  }
  return negative ? -value : value;
}

void clear_nested_rows(vec<vec<int>> &rows) {
  for (int i = 0; i < rows.size(); ++i)
    rows[i].clear(true);
  rows.clear(true);
}

} // namespace

Preprocessor::Preprocessor(const char *filename) : Preprocessor() {
  read_file(filename);
}

Preprocessor::~Preprocessor() { clear_problem(); }

void Preprocessor::clear_recovery_rows() {
  clear_nested_rows(res_clause);
  res_degree.clear(true);
  res_owner.clear(true);
  resolution.clear(true);
}

void Preprocessor::clear_problem() {
  release();
  delete[] mapto;
  delete[] mapval;
  mapto = nullptr;
  mapval = nullptr;

  clear_nested_rows(clause);
  degree.clear(true);
  clear_recovery_rows();

  vars = 0;
  clauses = 0;
  orivars = 0;
  res_clauses = 0;
  resolutions = 0;
}

void Preprocessor::read_file(const char *filename) {
  clear_problem();
  std::ifstream input(filename, std::ios::binary | std::ios::ate);
  const std::streamoff length = input.tellg();
  if (length < 0) {
    std::fprintf(stderr, "Cannot open input file %s\n", filename);
    std::exit(1);
  }
  auto data = std::make_unique_for_overwrite<char[]>(length + 1);
  input.seekg(0);
  if (!input.read(data.get(), length)) {
    std::fprintf(stderr, "Cannot read input file %s\n", filename);
    std::exit(1);
  }
  data[length] = '\0';

  clause.push();
  clause.push();
  int row = 1;
  bool read_bound = false;
  for (const char *p = read_whitespace(data.get()); *p; p = read_whitespace(p)) {
    if (*p == 'c') {
      while (*p && *p != '\n')
        ++p;
    } else if (*p == 'p') {
      p += 5;
      vars = read_int(p);
      clauses = read_int(p);
      degree.growTo(clauses + 1, 0);
    } else if (*p == 'k') {
      read_bound = true;
      ++p;
    } else {
      const int value = read_int(p);
      if (*p == '\0' && value != 0)
        std::exit(0);
      if (value == 0) {
        ++row;
        clause.push();
      } else if (read_bound) {
        degree[row] = value;
        read_bound = false;
      } else {
        clause[row].push(value);
      }
    }
  }

  clauses = row - 1;
  orivars = vars;
  for (int id = 1; id <= clauses; ++id)
    if (degree[id] == 0)
      degree[id] = 1;
}

void Preprocessor::preprocess_init() {
  color = new int[vars + 10];
  varval = new int[vars + 10];
  q = new int[vars + 10];
  seen = new int[(vars << 1) + 10];
  clause_delete.growTo(clauses + 1, 0);
  nxtc.growTo(clauses + 1, 0);
  occurp = new vec<int>[vars + 1];
  occurn = new vec<int>[vars + 1];
  resseen = new int[(vars << 1) + 10];

  mapval = new int[vars + 10];
  mapto = new int[vars + 10];
  for (int i = 1; i <= vars; i++)
    mapto[i] = i, mapval[i] = 0;
}

void Preprocessor::release() {
  delete[] color;
  color = nullptr;
  delete[] varval;
  varval = nullptr;
  delete[] q;
  q = nullptr;
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
    if (clause_delete[i])
      continue;
    ++id;
    if (i != id)
      clause[id].swap(clause[i]);
    for (int j = 0; j < clause[id].size(); ++j) {
      const int lit = clause[id][j];
      clause[id][j] = color[abs(lit)] * pnsign(lit);
    }
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

void Preprocessor::deduplicate_rows() {
  if (!solver_options.pre_dedup)
    return;

  clause_delete.growTo(clauses + 1, 0);
  clause_delete.setsize(clauses + 1);
  for (int id = 1; id <= clauses; ++id)
    clause_delete[id] = 0;

  std::vector<int> row, candidate;
  std::unordered_multimap<std::uint64_t, int> representatives;
  representatives.reserve(clauses);
  bool changed = false;
  for (int id = 1; id <= clauses; ++id) {
    row.assign(clause[id].data, clause[id].data + clause[id].size());
    std::sort(row.begin(), row.end());
    std::uint64_t hash = 1469598103934665603ULL;
    for (int lit : row)
      hash = (hash ^ static_cast<std::uint32_t>(lit)) * 1099511628211ULL;

    const auto [first, last] = representatives.equal_range(hash);
    auto match = first;
    for (; match != last; ++match) {
      const auto &other = clause[match->second];
      if (other.size() != clause[id].size())
        continue;
      candidate.assign(other.data, other.data + other.size());
      std::sort(candidate.begin(), candidate.end());
      if (row == candidate)
        break;
    }
    if (match == last) {
      representatives.emplace(hash, id);
    } else {
      changed = true;
      if (degree[match->second] >= degree[id]) {
        clause_delete[id] = 1;
      } else {
        clause_delete[match->second] = 1;
        match->second = id;
      }
    }
  }
  if (!changed)
    return;

  update_var_clause_label();
  delete[] mapfrom;
  mapfrom = new int[vars + 1]();
  for (int original = 1; original <= orivars; ++original) {
    if (!mapto[original])
      continue;
    const int mapped = pnsign(mapto[original]) * color[abs(mapto[original])];
    mapto[original] = mapped;
    if (mapped)
      mapfrom[abs(mapped)] = pnsign(mapped) * original;
    else if (!mapval[original])
      mapval[original] = 1;
  }
}

int Preprocessor::do_preprocess(const char *filename) {
  read_file(filename);
  preprocess_init();

  if (!preprocess_up()) {
    clear_problem();
    return 20;
  }

  deduplicate_rows();
  bool changed = false;
  if (!preprocess_resolution(changed) || !preprocess_binary(changed)) {
    clear_problem();
    return 20;
  }

  if (changed)
    deduplicate_rows();
  printf("c after preprocessing, vars = %d, clauses = %d\n", vars, clauses);

  release();
  return 0;
}
