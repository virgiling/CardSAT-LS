#pragma once

#include <string>

struct Options {
  std::string filename;

#define OPTION(name, type, initial, low, high, description) type name = initial;
#include "options.def"
#undef OPTION

  void parse_args(int argc, char *argv[]);
  void print_change();
};

inline Options solver_options;
