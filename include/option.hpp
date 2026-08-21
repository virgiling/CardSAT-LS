#pragma once
#ifndef _option_hpp_INCLUDED
#define _option_hpp_INCLUDED

#include "cmdline.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// name, type, short, must, default, low, high, comments
// The released CardSAT-LS profile fixes mode=0, preprocessing=1, seed=20,
// init_mode=1, and enable_swap=1 during tuning.  The command-line entries are
// retained so that controlled ablations and diagnostics remain reproducible.
// clang-format off
#define OPTIONS                                                                  \
  OPTION(cutoff, double, '\0', false, 3600.0, 0.0, 1e18,                         \
         "cutoff time(seconds)")                                                 \
  OPTION(seeds, int, '\0', false, 20, 0, 2e9, "random seed")                    \
  OPTION(preprocessing, int, '\0', false, 1, 0, 1,                                \
         "Preprocessing mode (0=none, 1=simplify)")                              \
  OPTION(mode, int, '\0', false, 0, 0, 1,                                         \
         "Use ccdcl or local search (0=local search, 1=ccdcl)")                  \
  OPTION(init_mode, int, '\0', false, 1, 0, 4,                                    \
         "Initialization mode (0=random, 1=backbone, 2=best, 3=false, 4=true)")  \
  OPTION(enable_swap, int, '\0', false, 1, 0, 1, "Enable swap variables")        \
  OPTION(ls_max_flips, int, '\0', false, 29256504, 0, 1e9,                       \
         "Maximum number of flips")                                              \
  OPTION(ls_max_non_improve_flip, int, '\0', false, 17193298, 0, 1e9,            \
         "Restart patience without a strict best improvement")                   \
  OPTION(ls_swap_non_improve_flip, int, '\0', false, 431188, 0, 1e9,             \
         "Non-improving flips before swap becomes eligible")                     \
  OPTION(ls_swapprob, double, '\0', false, 0.0044873770692, 0, 1,                 \
         "Swap probability")                                                     \
  OPTION(ls_backbone_threshold, int, '\0', false, 272, 0, 1e9,                    \
         "Backbone threshold")                                                   \
  OPTION(ls_rdprob, double, '\0', false, 0.015390844143, 0.0, 1.0,                \
         "Random descent probability")                                           \
  OPTION(ls_rwprob, double, '\0', false, 0.309754852086, 0.0, 1.0,                \
         "Random walk probability")                                              \
  OPTION(ls_hd_count_small, int, '\0', false, 20, 1, 10000,                      \
         "HD sample count for at most 2000 variables")                           \
  OPTION(ls_hd_count_large, int, '\0', false, 88, 1, 10000,                      \
         "HD sample count for more than 2000 variables")                         \
  OPTION(ls_swap_bms_cap, int, '\0', false, 2, 1, 10000,                         \
         "Maximum BMS samples for swap selection")                               \
  OPTION(ls_neighbor_budget_gib, int, '\0', false, 37, 1, 48,                    \
         "Neighbor-entry memory budget (GiB)")                                   \
  OPTION(verbose, int, '\0', false, 0, 0, 1, "Verbose mode")
// clang-format on

class Options {
public:
  std::string filename;

#define OPTION(N, T, S, M, D, L, H, C) T N = D;
  OPTIONS
#undef OPTION

  void parse_args(int argc, char *argv[]) {
    cmdline::parser parser;

#define OPTION(N, T, S, M, D, L, H, C)                                      \
  if (!strcmp(#T, "int"))                                                   \
    parser.add<int>(#N, S, C, M, (int)D, cmdline::range((int)L, (int)H));     \
  if (!strcmp(#T, "double"))                                                \
    parser.add<double>(#N, S, C, M, D, cmdline::range((double)L, (double)H));
    OPTIONS
#undef OPTION

    parser.footer("filename");
    parser.parse_check(argc, argv);

    if (parser.rest().size() == 0) {
        printf("filename is required\n");
        exit(1);
    }

    filename = parser.rest()[0];

#define OPTION(N, T, S, M, D, L, H, C)                                      \
  if (!strcmp(#T, "int"))                                                   \
    N = parser.get<int>(#N);                                                 \
  if (!strcmp(#T, "double"))                                                \
    N = parser.get<double>(#N);
    OPTIONS
#undef OPTION

    if (ls_hd_count_small > ls_hd_count_large) {
      std::fprintf(stderr, "--ls_hd_count_small must not exceed "
                           "--ls_hd_count_large\n");
      std::exit(1);
    }
  }

  void print_change() {
    printf(
        "----------------------------------------- Paras list "
        "-------------------------------------------------\n");
    printf("%-15s\t %-8s\t %-14s\t %-10s\t %s\n", "Name", "Type", "Now",
           "Default", "Comment");

#define OPTION(N, T, S, M, D, L, H, C)                                      \
  if (strcmp(#T, "int") == 0)                                                \
    printf("%-15s\t %-8s\t %-14d\t %-10s\t %s\n", (#N), (#T), (int)this->N, \
           (#D), (C));                                                        \
  if (strcmp(#T, "double") == 0)                                             \
    printf("%-15s\t %-8s\t %-14.12g\t %-10s\t %s\n", (#N), (#T),           \
           (double)this->N, (#D), (C));
    OPTIONS
#undef OPTION
    printf(
        "----------------------------------------------------------------------"
        "-------------------------------");
  }
};

inline Options __global_options;

inline void INIT_ARGS(int argc, char** argv) {
    __global_options.parse_args(argc, argv);
}

inline void PRINT_ARGS() {
  if (__global_options.verbose)
    __global_options.print_change();
}

#define OPT(opt) (__global_options.opt)

#define SETOPT(opt, val) (__global_options.opt = val)

#endif
