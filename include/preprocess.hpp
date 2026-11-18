#ifndef _preprocess_hpp_INCLUDED
#define _preprocess_hpp_INCLUDED

#include "vec.hpp"
#include <cstdlib>
#include <vector>

inline int pnsign(int x) { return (x > 0 ? 1 : -1); }
inline int tolit(int x) { return x > 0 ? ((x - 1) << 1) : ((-x - 1) << 1 | 1); }
inline int negative(int x) { return x ^ 1; }

struct Preprocessor {
  Preprocessor() = default;
  explicit Preprocessor(const char *filename);
  ~Preprocessor();

  Preprocessor(const Preprocessor &) = delete;
  Preprocessor &operator=(const Preprocessor &) = delete;
  Preprocessor(Preprocessor &&) = delete;
  Preprocessor &operator=(Preprocessor &&) = delete;

  void read_file(const char *filename);
  int do_preprocess(const char *filename);
  bool get_complete_model();

  int vars = 0, clauses = 0, orivars = 0;
  vec<vec<int>> clause, res_clause;
  vec<int> degree, res_degree, resolution, res_owner;
  int resolutions = 0, res_clauses = 0;
  int *mapto = nullptr, *mapval = nullptr;

private:
  struct PureLiteralQueue {
    std::vector<int> positive, negative, pending;
    std::vector<unsigned char> queued, eliminated;

    explicit PureLiteralQueue(int variables)
        : positive(variables + 1), negative(variables + 1), queued(variables + 1),
          eliminated(variables + 1) {}

    int &count(int literal) {
      return literal > 0 ? positive[literal] : negative[-literal];
    }

    void enqueue(int variable) {
      if (!eliminated[variable] && !queued[variable]) {
        pending.push_back(variable);
        queued[variable] = 1;
      }
    }

    void remove_literal(int literal) {
      --count(literal);
      enqueue(std::abs(literal));
    }
  };

  int *color = nullptr, *varval = nullptr, *q = nullptr, *seen = nullptr,
      *resseen = nullptr, *mapfrom = nullptr;
  vec<int> *occurp = nullptr, *occurn = nullptr;
  vec<int> clause_delete, nxtc;

  void update_var_clause_label();
  void preprocess_init();
  void release();
  bool preprocess_resolution(bool &changed);
  bool preprocess_binary(bool &changed);
  bool preprocess_up();
  bool simplify_clause(int id, int &tail);
  bool recovery_clauses_satisfied(int first, int last, int pivot,
                                  int value) const;
  bool simplify_pure_literals(PureLiteralQueue &candidates, bool &changed);
  void remove_resolution_parent(int id, int owner,
                                PureLiteralQueue &candidates);
  void deduplicate_rows();
  void clear_problem();
  void clear_recovery_rows();
};

#endif
