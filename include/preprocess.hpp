#ifndef _preprocess_hpp_INCLUDED
#define _preprocess_hpp_INCLUDED

#include "vec.hpp"

inline int pnsign(int x) { return (x > 0 ? 1 : -1); }
inline int tolit(int x) { return x > 0 ? ((x - 1) << 1) : ((-x - 1) << 1 | 1); }
inline int negative(int x) { return x ^ 1; }
inline int toiidx(int x) { return (x >> 1) + 1; }
inline int toeidx(int x) { return (x & 1 ? -toiidx(x) : toiidx(x)); }

struct Preprocessor {
public:
  Preprocessor();
  explicit Preprocessor(const char *filename);
  ~Preprocessor();

  Preprocessor(const Preprocessor &) = delete;
  Preprocessor &operator=(const Preprocessor &) = delete;
  Preprocessor(Preprocessor &&) = delete;
  Preprocessor &operator=(Preprocessor &&) = delete;

  int vars;
  int clauses;
  vec<vec<int>> clause, res_clause;
  vec<int> degree, in_cardinality, res_degree;
  void write_to_file(const char *filename);
  void release();

  int maxlen, orivars, oriclauses, res_clauses, resolutions;
  int *f = nullptr, *val = nullptr, *color = nullptr, *varval = nullptr,
      *q = nullptr, *seen = nullptr, *resseen = nullptr, *clean = nullptr,
      *mapto = nullptr, *mapfrom = nullptr, *mapval = nullptr;
  vec<int> *occurp = nullptr, *occurn = nullptr;
  vec<int> clause_delete, nxtc, resolution;
  vec<int> res_owner;

  int find(int x);
  void update_var_clause_label();
  void preprocess_init();
  bool preprocess_resolution();
  bool preprocess_binary();
  bool preprocess_card();
  bool preprocess_up();
  bool get_complete_model();
  int do_preprocess(const char *filename,
                    bool enable_local_search_passes = false);

private:
  void clear_problem();
  void read_file(const char *filename);
};

#endif
