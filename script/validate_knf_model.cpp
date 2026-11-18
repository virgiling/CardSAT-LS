// Independent, streaming validation of a recovered model against the input KNF.
// Usage: validate_knf_model INPUT.knf SOLVER.log
// Exit 0: valid; 2: invalid/missing model; 3: malformed/unreadable input.
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class Tokens {
  FILE *file_;
  char buffer_[65536];
  size_t position_ = 0, size_ = 0;
  int last_ = '\n';

public:
  explicit Tokens(const char *path) : file_(std::fopen(path, "rb")) {
    if (!file_)
      throw std::runtime_error("cannot_open_file");
  }
  ~Tokens() { std::fclose(file_); }
  int get() {
    if (position_ == size_) {
      size_ = std::fread(buffer_, 1, sizeof(buffer_), file_);
      position_ = 0;
      if (!size_) {
        if (std::ferror(file_))
          throw std::runtime_error("read_error");
        return last_ = EOF;
      }
    }
    return last_ = static_cast<unsigned char>(buffer_[position_++]);
  }
  bool next(std::string &token) {
    token.clear();
    int ch;
    do {
      ch = get();
    } while (ch != EOF && ch <= ' ');
    if (ch == EOF)
      return false;
    do {
      token.push_back(static_cast<char>(ch));
      if (token.size() > 256)
        throw std::runtime_error("oversized_token");
      ch = get();
    } while (ch != EOF && ch > ' ');
    return true;
  }
  void skip_line() {
    while (last_ != EOF && last_ != '\n')
      get();
  }
};

long long integer(const std::string &token) {
  if (token.empty())
    throw std::runtime_error("missing_integer");
  char *end = nullptr;
  errno = 0;
  const long long value = std::strtoll(token.c_str(), &end, 10);
  if (errno || end == token.c_str() || *end)
    throw std::runtime_error("invalid_integer");
  return value;
}

long long next_integer(Tokens &tokens) {
  std::string token;
  if (!tokens.next(token))
    throw std::runtime_error("unexpected_eof");
  return integer(token);
}

int report(bool valid, const char *reason, int code, long long row = 0) {
  std::cout << "{\"valid\":" << (valid ? "true" : "false")
            << ",\"reason\":\"" << reason << "\",\"row\":" << row << "}\n";
  return code;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3)
    return report(false, "usage", 3);
  try {
    Tokens input(argv[1]);
    std::string token;
    bool header = false;
    long long variables = 0, declared_rows = 0;
    while (input.next(token)) {
      if (token == "c") {
        input.skip_line();
      } else if (token == "p") {
        if (!input.next(token) || (token != "cnf" && token != "knf"))
          return report(false, "unsupported_header", 3);
        variables = next_integer(input);
        declared_rows = next_integer(input);
        if (variables < 0 || variables > INT_MAX || declared_rows < 0)
          return report(false, "invalid_header", 3);
        header = true;
        break;
      } else {
        return report(false, "missing_header", 3);
      }
    }
    if (!header)
      return report(false, "missing_header", 3);

    std::vector<unsigned char> assignment(static_cast<size_t>(variables) + 1, 0);
    bool found_model = false, terminated = false;
    long long assigned = 0;
    try {
      Tokens output(argv[2]);
      while (output.next(token)) {
        if (token != "v") {
          output.skip_line();
          continue;
        }
        if (terminated)
          return report(false, "multiple_models", 2);
        found_model = true;
        while (output.next(token)) {
          if (token == "v")
            continue; // DIMACS model continuation lines.
          const long long lit = integer(token);
          if (!lit) {
            terminated = true;
            break;
          }
          if (lit < -variables || lit > variables)
            return report(false, "model_variable_out_of_range", 2);
          const size_t var = static_cast<size_t>(lit > 0 ? lit : -lit);
          const unsigned char value = lit > 0 ? 1 : 2;
          if (assignment[var])
            return report(false, assignment[var] == value ? "duplicate_assignment"
                                                         : "conflicting_assignment", 2);
          assignment[var] = value;
          ++assigned;
        }
      }
    } catch (const std::bad_alloc &) {
      return report(false, "validator_memory_limit", 3);
    } catch (const std::exception &) {
      return report(false, "malformed_model", 2);
    }
    if (!found_model || !terminated || assigned != variables)
      return report(false, "incomplete_model", 2);

    long long rows = 0, bound = 1, true_literals = 0;
    bool in_row = false;
    while (input.next(token)) {
      if (token == "c") {
        input.skip_line();
        continue;
      }
      if (token == "k") {
        if (in_row)
          return report(false, "nested_cardinality_row", 3, rows + 1);
        bound = next_integer(input);
        if (bound < 0)
          return report(false, "negative_bound", 3, rows + 1);
        in_row = true;
        continue;
      }
      const long long lit = integer(token);
      in_row = true;
      if (!lit) {
        ++rows;
        if (true_literals < bound)
          return report(false, "unsatisfied_row", 2, rows);
        bound = 1;
        true_literals = 0;
        in_row = false;
      } else {
        if (lit < -variables || lit > variables)
          return report(false, "input_variable_out_of_range", 3, rows + 1);
        const size_t var = static_cast<size_t>(lit > 0 ? lit : -lit);
        if (assignment[var] == (lit > 0 ? 1 : 2))
          ++true_literals;
      }
    }
    if (in_row || rows != declared_rows)
      return report(false, "input_row_count_mismatch", 3, rows);
    return report(true, "verified", 0, rows);
  } catch (const std::bad_alloc &) {
    return report(false, "validator_memory_limit", 3);
  } catch (const std::exception &) {
    return report(false, "malformed_or_unreadable_input", 3);
  }
}
