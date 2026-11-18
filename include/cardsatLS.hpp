#pragma once

#include <cstdint>
#include <sys/times.h>
#include <vector>

struct Preprocessor;

namespace LocalSearch {

constexpr int MY_RAND_MAX_INT = 10000000;

inline int clause_score_contribution(bool lit_true, int sat_count,
                                     int threshold, int weight) {
  if (lit_true)
    return sat_count <= threshold ? -weight : 0;
  return sat_count < threshold ? weight : 0;
}

struct lit {
  int clause_num; // Zero-based.
  int var_num;    // One-based.
  bool sense;     // True for a positive literal.
};

class CardSATLS {
public:
  CardSATLS() = default;
  ~CardSATLS() { free_memory(); }
  CardSATLS(const CardSATLS &) = delete;
  CardSATLS &operator=(const CardSATLS &) = delete;

  int *temp_array;
  struct tms start_time;

  int64_t total_step;

  int num_vars;
  int num_clauses;
  int max_clause_length = 0;

  int tries;
  int max_tries;
  unsigned int max_flips;
  unsigned int max_non_improve_flip;
  unsigned int swap_non_improve_flip;
  unsigned int step;

  double opt_time;

  lit **var_lit = nullptr;
  int *var_lit_count;
  lit **clause_lit;
  int *clause_lit_count;
  int *clause_true_lit_thres;

  int *score;
  int *time_stamp;
  int **var_neighbor;
  int *var_neighbor_count;
  double avg_neighbor_lit = 0;

  int *unit_weight;
  int *weight_delta_score;
  int *weight_touched_vars;
  bool *weight_is_touched;
  bool enable_weight_transfer = false;
  int weight_update_period = 4;
  int weight_work_budget = 2048;
  int delta_total_weight;
  int ave_weight;
  float p_scale;
  float q_scale;
  int scale_ave;
  int q_init = 0;

  int *sat_count;
  int *sat_var;

  // NobleCount applies only to CNF rows with distinct variables.
  bool enable_noble_count = false;
  std::vector<uint32_t> noble_true_xor;
  std::vector<unsigned char> noble_eligible;
  static uint32_t noble_literal_id(const lit &occurrence) {
    return (static_cast<uint32_t>(occurrence.var_num) << 1) |
           static_cast<uint32_t>(occurrence.sense);
  }

  int *hardunsat_stack;
  int *index_in_hardunsat_stack;
  int unsat_count;

  int *cardinalitysat_stack;
  int *index_in_cardinalitysat_stack;
  int satisfied_cardinality_count;

  int *goodvar_stack;
  int goodvar_count;
  int *already_in_goodvar_stack;

  int *cur_soln; // 0 = false, 1 = true.
  // Monotone diagnostic progress for tuning; it never affects search choices.
  int minimum_hard_unsat_nb;

  // Algorithm parameters
  int rwprob_threshold;
  int rdprob_threshold;
  int hd_count_threshold;
  int swap_bms_cap;
  bool enable_joint_swap = false;
  int joint_swap_samples = 8;
  int joint_swap_work_budget = 4096;
  double swapprob;
  int swapprob_threshold;
  int backbone_threshold;
  uint64_t m_prng_state;

  bool enable_swap;

  bool parse_from_preprocess(const Preprocessor &input);
  int solve();
  void local_search();
  int local_search_flip();
  void free_memory();
  void start_timing();
  double get_runtime();

  // Setup and verification
  void allocate_memory();
  int verify_sol();

  // Clause and weight updates
  void increase_weights();
  void smooth_weights();
  void update_clause_weights();
  void unsat(int clause);
  void sat(int clause);
  void init_local_search();

  // Variable selection and flipping
  void update_goodvarstack(int flipvar);
  void settings();
  void set_init_method(int mode) { init_mode = mode; }

  int pick_var();
  void swap_vars();
  void flip_pair_atomic(int first_var, int second_var);
  inline uint32_t fast_rand() {
    m_prng_state ^= m_prng_state << 13;
    m_prng_state ^= m_prng_state >> 7;
    m_prng_state ^= m_prng_state << 17;
    return static_cast<uint32_t>(m_prng_state);
  }

  void flip(int variable);
  bool materialized_neighbor_mode = false;
  void init_assignment();

  void add_goodvar(int v) {
    already_in_goodvar_stack[v] = goodvar_count;
    goodvar_stack[goodvar_count++] = v;
  }

  void remove_goodvar(int v) {
    const int index = already_in_goodvar_stack[v];
    const int last = goodvar_stack[--goodvar_count];
    goodvar_stack[index] = last;
    already_in_goodvar_stack[last] = index;
    already_in_goodvar_stack[v] = -1;
  }

private:
  void prepare_noble_count();
  lit *literal_storage = nullptr;
  void prepare_neighbors(uint64_t entry_budget);
  bool build_neighbor_relation(uint64_t entry_budget);

  int init_mode = 1;
  int compare_variables(int candidate, int incumbent) const {
    if (score[candidate] != score[incumbent])
      return score[candidate] > score[incumbent] ? 1 : -1;
    if (time_stamp[candidate] != time_stamp[incumbent])
      return time_stamp[candidate] < time_stamp[incumbent] ? 1 : -1;
    return 0;
  }

  int select_var_after_update_weight();
  int weight_update_tick = 0;
  bool try_joint_swap();
  bool try_transfer_weights();
  void accumulate_weight_delta(int clause, int delta, int &touched_count);
  void apply_weight_score_deltas(int touched_count);
  void apply_weight_score_delta(int v, int delta);
};

} // namespace LocalSearch
