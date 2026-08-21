#pragma once

#ifndef _ccdcl_hpp_INCLUDED
#define _ccdcl_hpp_INCLUDED

#include "cadical.hpp"
#include "cardsatLS.hpp"
#include "preprocess.hpp"
#include <chrono>
#include <cstdio>
#include <stdexcept>

class CCDCLSolver : public CaDiCaL::CardsatLS {
public:
  CCDCLSolver(int seed, int init_mode = 1, int enable_swap = 1,
              unsigned int ls_max_flips = 29256504,
              unsigned int ls_max_non_improve_flip = 17193298,
              double ls_swapprob = 0.0044873770692,
              int ls_backbone_threshold = 1,
              unsigned int ls_swap_non_improve_flip = 431188) {
    m_seed = seed;
    m_init_mode = init_mode;
    m_enable_swap = enable_swap == 1;
    m_ls_max_flips = ls_max_flips;
    m_ls_max_non_improve_flip = ls_max_non_improve_flip;
    m_ls_swap_non_improve_flip = ls_swap_non_improve_flip;
    m_ls_swapprob = ls_swapprob;
    m_ls_backbone_threshold = ls_backbone_threshold;
    srand(seed);
    m_solver = new CaDiCaL::Solver();
    m_ls_solver = new LocalSearch::CardSATLS();
    m_ls_solver->q_init = 0;
    m_solver->set("seed", seed);
    m_solver->set("ccdclMode", 1);
    if (!m_solver->limit("localsearch", 1))
      throw std::logic_error("CaDiCaL rejected the localSearch limit");
    m_solver->set_long_option("--no-binary");
    m_solver->connect_cardsat_ls(this);
  }

  ~CCDCLSolver() {
    m_solver->disconnect_cardsat_ls();
    delete m_ls_solver;
    delete m_solver;
  }

  void allocate_memory(int num_vars, int num_clauses) override {
    actual_num_vars = num_vars;
    actual_num_clauses = num_clauses;
    m_ls_solver->num_vars = num_vars + 10;
    m_ls_solver->num_clauses = num_clauses + 10;
    m_ls_solver->max_clause_length = 0;
    m_ls_solver->allocate_memory();
    c = 0;
  }

  void add_clause(const int *clause, int size, int degree) override {
    m_ls_solver->clause_lit_count[c] = size;
    m_ls_solver->clause_true_lit_thres[c] = degree;
    m_ls_solver->clause_lit[c] = new LocalSearch::lit[size + 1];
    for (int i = 0; i < size; i++) {
      m_ls_solver->clause_lit[c][i].clause_num = c;
      m_ls_solver->clause_lit[c][i].var_num = abs(clause[i]);
      m_ls_solver->clause_lit[c][i].sense = clause[i] > 0 ? 1 : 0;
      m_ls_solver->var_lit_count[m_ls_solver->clause_lit[c][i].var_num]++;
    }
    m_ls_solver->max_clause_length =
        std::max(m_ls_solver->max_clause_length, size);
    m_ls_solver->clause_lit[c][size].var_num = 0;
    m_ls_solver->clause_lit[c][size].clause_num = -1;
    c++;
  }

  void init_assignment(const std::vector<signed char> &assignment) override {
    for (size_t i = 0; i < assignment.size(); i++) {
      m_ls_solver->cur_soln[i + 1] = assignment[i] > 0 ? 1 : 0;
      m_ls_solver->time_stamp[i + 1] = 0;
    }
  }

  void get_phase(std::vector<signed char> &phases) override {
    for (size_t i = 1; i < phases.size(); i++) {
      phases[i] = m_ls_solver->cur_soln[i] > 0 ? 1 : -1;
    }
  }

  int start_cardsat_ls(int limit) override {
    (void)limit;
    ++m_ls_callback_count;
    m_ls_solver->num_vars = actual_num_vars;
    m_ls_solver->num_clauses = c;

    long long tmp_lit_num = 0;
    for (int v = 1; v <= m_ls_solver->num_vars; ++v) {
      m_ls_solver->var_lit[v] =
          new LocalSearch::lit[m_ls_solver->var_lit_count[v] + 1];
      tmp_lit_num += m_ls_solver->var_lit_count[v];
      m_ls_solver->var_lit_count[v] = 0;
    }
    for (int clause = 0; clause < m_ls_solver->num_clauses; ++clause) {
      for (int i = 0; i < m_ls_solver->clause_lit_count[clause]; ++i) {
        const LocalSearch::lit occurrence =
            m_ls_solver->clause_lit[clause][i];
        const int variable = occurrence.var_num;
        const int occurrence_index = m_ls_solver->var_lit_count[variable]++;
        m_ls_solver->var_lit[variable][occurrence_index] = occurrence;
      }
    }
    m_ls_solver->avg_neighbor_lit =
        double(tmp_lit_num - m_ls_solver->num_clauses) /
        (m_ls_solver->num_vars - m_ls_solver->num_clauses + 1);
    srand(m_seed);

    // settings() derives thresholds and callbacks from these values.
    m_ls_solver->set_init_method(m_init_mode);
    m_ls_solver->enable_swap = m_enable_swap;
    m_ls_solver->max_flips = m_ls_max_flips;
    m_ls_solver->max_non_improve_flip = m_ls_max_non_improve_flip;
    m_ls_solver->swap_non_improve_flip = m_ls_swap_non_improve_flip;
    m_ls_solver->swapprob = m_ls_swapprob;
    m_ls_solver->backbone_threshold = m_ls_backbone_threshold;
    m_ls_solver->settings();
    std::printf(
        "c audit ccdcl_ls_parameters init_mode %d enable_swap %d "
        "ls_max_flips %u ls_max_non_improve_flip %u "
        "ls_swap_non_improve_flip %u ls_swapprob %.17g "
        "ls_backbone_threshold %d swapprob_threshold %d\n",
        m_init_mode, m_enable_swap ? 1 : 0, m_ls_solver->max_flips,
        m_ls_solver->max_non_improve_flip,
        m_ls_solver->swap_non_improve_flip, m_ls_solver->swapprob,
        m_ls_solver->backbone_threshold, m_ls_solver->swapprob_threshold);
    m_ls_solver->tries = 1;
    m_ls_solver->init_assignment();
    m_ls_solver->init_local_search();
    return m_ls_solver->local_search_flip();
  }

  void free_memory() override { m_ls_solver->free_memory(); }

  void parse_from_preprocess(Preprocessor *preprocessor) {
    int native_rows = 0;
    for (int i = 1; i <= preprocessor->clauses; ++i)
      native_rows += preprocessor->degree[i] > 1 ? 1 : 0;
    std::printf("c audit ccdcl_hybrid_mode %d\n", configured_mode());
    std::printf(
        "c audit ccdcl_mode native_rows %d encoding_rows 0 configured_mode "
        "%d\n",
        native_rows, configured_mode());
    m_solver->reserve(preprocessor->vars);
    for (int i = 1; i <= preprocessor->clauses; i++) {
      int l = preprocessor->clause[i].size();
      m_solver->CARadd(preprocessor->degree[i]);
      for (int j = 0; j < l; j++) {
        m_solver->CARadd(preprocessor->clause[i][j]);
      }
      m_solver->CARadd(0);
    }
  }

  int solve() {
    start_time = std::chrono::high_resolution_clock::now();
    int res = m_solver->solve();
    opt_time = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::high_resolution_clock::now() - start_time)
                   .count();
    return res;
  }

  int val(int lit) { return m_solver->val(lit); }
  int configured_mode() const { return m_solver->get("ccdclMode"); }

public:
  CaDiCaL::Solver *m_solver;
  LocalSearch::CardSATLS *m_ls_solver;
  int m_seed;
  int m_init_mode = 1;
  bool m_enable_swap = true;
  unsigned int m_ls_max_flips = 29256504;
  unsigned int m_ls_max_non_improve_flip = 17193298;
  unsigned int m_ls_swap_non_improve_flip = 431188;
  double m_ls_swapprob = 0.0044873770692;
  int m_ls_backbone_threshold = 272;
  int m_ls_callback_count = 0;
  int c = 0;
  int actual_num_vars = 0;
  int actual_num_clauses = 0;
  double opt_time;
  std::chrono::high_resolution_clock::time_point start_time;
};

#endif // _ccdcl_hpp_INCLUDED
