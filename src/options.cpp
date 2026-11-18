#include "option.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

namespace {

struct NumericOption {
  const char *name;
  const char *description;
  const char *initial_text;
  double initial;
  double low;
  double high;
  std::variant<int *, double *> field;
  bool supplied = false;
};

auto option_table(Options &values) {
  return std::array{
#define OPTION(name, type, initial, low, high, description)                    \
  NumericOption{#name, description, #initial, initial, low, high, &values.name},
#include "options.def"
#undef OPTION
  };
}

NumericOption *find_option(std::span<NumericOption> table,
                           std::string_view name) {
  for (auto &option : table)
    if (name == option.name)
      return &option;
  return nullptr;
}

template <typename T>
bool read_number(const char *text, const NumericOption &option, T &field) {
  std::istringstream input(text);
  T value;
  if (!(input >> value) || !input.eof() ||
      !(value >= option.low && value <= option.high))
    return false;
  field = value;
  return true;
}

std::string read_long_option(int &index, int argc, char *argv[],
                             std::span<NumericOption> table, bool &help) {
  const std::string_view argument(argv[index]);
  const auto equals = argument.find('=');
  const auto name = argument.substr(2, equals - 2);
  if (name == "help") {
    if (equals != argument.npos)
      return "option value is invalid: " + std::string(argument);
    help = true;
    return {};
  }

  NumericOption *option = find_option(table, name);
  if (!option)
    return "undefined option: --" + std::string(name);

  const char *value;
  if (equals != argument.npos)
    value = argv[index] + equals + 1;
  else if (index + 1 < argc)
    value = argv[++index];
  else
    return "option needs value: --" + std::string(name);

  if (auto *field = std::get_if<int *>(&option->field))
    option->supplied = read_number(value, *option, **field);
  else
    option->supplied =
        read_number(value, *option, *std::get<double *>(option->field));
  if (!option->supplied)
    return "option value is invalid: --" + std::string(name) + "=" + value;
  return {};
}

void print_usage(const char *program, std::span<const NumericOption> table) {
  std::fprintf(stderr, "usage: %s [options] ... filename\noptions:\n", program);
  int width = 4;
  for (const auto &option : table)
    width =
        std::max(width, static_cast<int>(std::string_view(option.name).size()));
  width += 4;
  for (const auto &option : table) {
    std::fprintf(stderr, "      --%-*s%s", width, option.name,
                 option.description);
    if (std::holds_alternative<int *>(option.field))
      std::fprintf(stderr, " (int [=%d])\n", static_cast<int>(option.initial));
    else
      std::fprintf(stderr, " (double [=%g])\n", option.initial);
  }
  std::fprintf(stderr, "  -?, --%-*sprint this message\n", width, "help");
}

} // namespace

void Options::parse_args(int argc, char *argv[]) {
  *this = Options{};
  auto table = option_table(*this);
  bool help = false;
  const char *input_file = nullptr;
  std::string error;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg.starts_with("--")) {
      std::string result = read_long_option(i, argc, argv, table, help);
      if (error.empty())
        error = std::move(result);
    } else if (arg.starts_with('-')) {
      for (char key : arg.substr(1)) {
        if (key == '?')
          help = true;
        else if (error.empty())
          error = std::string("undefined short option: -") + key;
      }
    } else if (!input_file) {
      input_file = argv[i];
    }
  }
  if (help || !error.empty()) {
    if (!help)
      std::fprintf(stderr, "%s\n", error.c_str());
    print_usage(argv[0], table);
    std::exit(help ? 0 : 1);
  }
  if (!input_file) {
    std::puts("filename is required");
    std::exit(1);
  }
  filename = input_file;
  if (ls_hd_count_small > ls_hd_count_large) {
    std::fputs("--ls_hd_count_small must not exceed --ls_hd_count_large\n",
               stderr);
    std::exit(1);
  }
  if (mode && ls_joint_swap && find_option(table, "ls_joint_swap")->supplied) {
    std::fputs("Joint swap requires --mode 0\n", stderr);
    std::exit(1);
  }
  if (mode) {
    ls_joint_swap = 0;
  }
}

void Options::print_change() {
  std::puts("----------------------------------------- Paras list "
            "-------------------------------------------------");
  std::printf("%-15s\t %-8s\t %-14s\t %-10s\t %s\n", "Name", "Type", "Now",
              "Default", "Comment");
  for (const auto &option : option_table(*this)) {
    if (const auto *field = std::get_if<int *>(&option.field))
      std::printf("%-15s\t %-8s\t %-14d\t %-10s\t %s\n", option.name, "int",
                  **field, option.initial_text, option.description);
    else
      std::printf("%-15s\t %-8s\t %-14.12g\t %-10s\t %s\n", option.name,
                  "double", *std::get<double *>(option.field),
                  option.initial_text, option.description);
  }
  std::printf(
      "----------------------------------------------------------------------"
      "-------------------------------");
}
