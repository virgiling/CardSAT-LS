#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

// 高效的字符串转整数（避免使用 stoi 的开销）
inline long long parse_long(const char *str, char **endptr) {
  return strtoll(str, endptr, 10);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    cerr << "Usage: " << argv[0] << " <input.wecnf> <output.wcnf>" << endl;
    return 1;
  }

  const char *input_file = argv[1];
  const char *output_file = argv[2];

  ifstream fin(input_file, ios::binary);
  if (!fin.is_open()) {
    cerr << "Error: Cannot open input file: " << input_file << endl;
    return 1;
  }

  ofstream fout(output_file, ios::binary);
  if (!fout.is_open()) {
    cerr << "Error: Cannot open output file: " << output_file << endl;
    return 1;
  }

  string line;
  long long n_vars = 0, n_clauses = 0, top = 0;
  bool header_found = false;

  // 单遍处理：边读边写
  while (getline(fin, line)) {
    if (line.empty()) {
      fout << '\n';
      continue;
    }

    if (line[0] == 'c') {
      // 注释行，直接输出
      fout << line << '\n';
      continue;
    }

    if (line[0] == 'p') {
      // 解析头部：p wcnf/wecnf n_vars n_clauses top
      istringstream iss(line);
      string p, format;
      iss >> p >> format;

      if (format == "wcnf" || format == "wecnf") {
        iss >> n_vars >> n_clauses >> top;
        header_found = true;
        // 输出标准 WCNF 头部
        fout << "p wcnf " << n_vars << " " << n_clauses << " " << top << '\n';
        continue;
      } else {
        cerr << "Error: Unsupported format: " << format << endl;
        return 1;
      }
    }

    // 处理子句：weight literal1 literal2 ... 0
    const char *str = line.c_str();
    char *endptr;

    // 读取权重
    long long weight = parse_long(str, &endptr);
    if (endptr == str) {
      continue; // 空行或无效行
    }

    if (!header_found) {
      cerr << "Error: Clause found before header" << endl;
      return 1;
    }

    // 判断是硬子句还是软子句
    // 硬子句：权重 >= top，软子句：权重 < top
    long long output_weight;
    if (weight >= top) {
      output_weight = top; // 硬子句保持 top
    } else {
      output_weight = 1; // 软子句改为 1
    }

    // 输出权重
    fout << output_weight;

    // 读取并输出所有文字，直到遇到 0
    const char *p = endptr;
    while (*p != '\0') {
      // 跳过空白字符
      while (*p == ' ' || *p == '\t') {
        p++;
      }
      if (*p == '\0')
        break;

      // 读取数字
      long long lit = parse_long(p, &endptr);
      if (endptr == p)
        break;

      fout << ' ' << lit;

      if (lit == 0) {
        break; // 子句结束
      }
      p = endptr;
    }

    fout << '\n';
  }

  if (!header_found) {
    cerr << "Error: Header not found" << endl;
    return 1;
  }

  fin.close();
  fout.close();

  return 0;
}
