import glob
import os
from concurrent.futures import ThreadPoolExecutor, as_completed

from tqdm import tqdm

DIMACS = "mobsfile"
CARDINALITY = "faultfile"


class CNF:
    def __init__(self):
        self.clauses = []
        self.cardinality = []
        self.variables = set()
        self.num_var = 0
        self.num_clause = 0

    def parse_dimacs(self, filename):
        with open(filename, "r") as f:
            for line in f:
                if line.startswith("p"):
                    self.num_var = int(line.split()[2])
                    self.num_clause = int(line.split()[3])
                elif line.startswith("c"):
                    continue
                else:
                    self.num_clause += 1
                    self.variables.update(
                        list(map(lambda x: abs(int(x)), line.split()[:-1]))
                    )
                    self.clauses.append(line.split()[:-1])

    def parse_cardinality(self, filename):
        with open(filename, "r") as f:
            for line in f:
                x = line.split()[0]
                var = abs(int(x))
                self.variables.add(var)
                self.cardinality.append(var)

    def parse_ccfile(self, filename):
        if not os.path.exists(filename):
            return

        with open(filename, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("c") or line.startswith("p"):
                    continue

                clause = line.split()
                if clause and clause[-1] == "0":
                    clause = clause[:-1]

                if clause:
                    self.variables.update(list(map(lambda x: abs(int(x)), clause)))
                    self.clauses.append(clause)
                    self.num_clause += 1

    def parse_ccfile_j(self, filename):
        """读取 ccfile.j 到 clauses 中，并更新 variables"""
        if not os.path.exists(filename):
            return

        with open(filename, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("c") or line.startswith("p"):
                    continue

                clause = line.split()
                if clause and clause[-1] == "0":
                    clause = clause[:-1]

                if clause:
                    self.variables.update(list(map(lambda x: abs(int(x)), clause)))
                    self.clauses.append(clause)
                    self.num_clause += 1

    def write_cnf(self, filename):
        with open(filename, "w") as f:
            f.write(f"p cnf {len(self.variables)} {self.num_clause}\n")
            for clause in self.clauses:
                f.write(" ".join(str(l) for l in clause) + " 0\n")

    def write_knf(self, filename, bound):
        """生成 KNF 文件，使用 bound 参数更新基数约束"""
        with open(filename, "w") as f:
            f.write(f"p knf {len(self.variables)} {self.num_clause + 1}\n")
            for clause in self.clauses:
                f.write(" ".join(str(l) for l in clause) + " 0\n")
            f.write(
                "k "
                + str(len(self.cardinality) - bound)
                + " "
                + " ".join(str(-l) for l in self.cardinality)
                + " 0\n"
            )


def find_max_ccfile_index(folder_path):
    pattern = os.path.join(folder_path, "ccfile.*")
    ccfiles = glob.glob(pattern)
    if not ccfiles:
        return 0

    max_index = -1
    for ccfile in ccfiles:
        basename = os.path.basename(ccfile)
        if basename.startswith("ccfile."):
            try:
                index = int(basename.split(".")[-1])
                max_index = max(max_index, index)
            except ValueError:
                continue

    return max_index if max_index >= 0 else 0


def process_folder(folder_path, base_output_dir, generate_cnf=False):
    try:
        mobsfile_path = os.path.join(folder_path, DIMACS)
        faultfile_path = os.path.join(folder_path, CARDINALITY)
        ccfile_path = os.path.join(folder_path, "ccfile")

        if not os.path.exists(mobsfile_path) or not os.path.exists(faultfile_path):
            return f"跳过 {folder_path}: 缺少必要文件"

        # 找到最大的 ccfile.j 的 j 值
        max_bound = find_max_ccfile_index(folder_path)
        ccfile_j_path = (
            os.path.join(folder_path, f"ccfile.{max_bound}") if max_bound >= 0 else None
        )

        cnf = CNF()
        cnf.parse_dimacs(mobsfile_path)
        cnf.parse_cardinality(faultfile_path)

        folder_name = os.path.basename(folder_path.rstrip("/"))
        parent_folder = os.path.basename(os.path.dirname(folder_path))

        os.makedirs(base_output_dir, exist_ok=True)

        results = []

        output_filename_knf = f"{parent_folder}_{folder_name}.knf"
        output_path_knf = os.path.join(base_output_dir, output_filename_knf)
        cnf.write_knf(output_path_knf, max_bound)
        results.append(f"KNF: {output_path_knf}")

        if generate_cnf:
            cnf_for_cnf = CNF()
            cnf_for_cnf.parse_dimacs(mobsfile_path)
            if os.path.exists(ccfile_path):
                cnf_for_cnf.parse_ccfile(ccfile_path)
            if ccfile_j_path and os.path.exists(ccfile_j_path):
                cnf_for_cnf.parse_ccfile_j(ccfile_j_path)

            output_filename_cnf = f"{parent_folder}_{folder_name}.cnf"
            output_path_cnf = os.path.join(base_output_dir, output_filename_cnf)
            cnf_for_cnf.write_cnf(output_path_cnf)
            results.append(f"CNF: {output_path_cnf}")

        return f"Success: {folder_path} -> {'; '.join(results)}"
    except Exception as e:
        return f"ERROR: {folder_path}: {str(e)}"


def find_all_folders(base_dir):
    folders = []
    for root, dirs, files in os.walk(base_dir):
        if DIMACS in files and CARDINALITY in files:
            folders.append(root)
    return folders


def main(base_dir, output_dir, num_threads=4, generate_cnf=False):
    folders = find_all_folders(base_dir)
    if not folders:
        return

    with ThreadPoolExecutor(max_workers=num_threads) as executor:
        future_to_folder = {
            executor.submit(process_folder, folder, output_dir, generate_cnf): folder
            for folder in folders
        }

        results = []
        with tqdm(total=len(folders), desc="Processing") as pbar:
            for future in as_completed(future_to_folder):
                folder = future_to_folder[future]
                try:
                    result = future.result()
                    results.append(result)
                except Exception as e:
                    results.append(f"ERROR: {folder}: {str(e)}")
                pbar.update(1)

    success_count = sum(1 for r in results if r.startswith("Success"))
    error_count = len(results) - success_count
    print(f"Success: {success_count}, Failed/Skipped: {error_count}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="将 DES 格式转换为 KNF 格式")
    parser.add_argument(
        "--base_dir", type=str, default="DES/cc1", help="基础目录路径（默认: DES/cc1）"
    )
    parser.add_argument(
        "--output_dir", type=str, default="KNF", help="输出目录路径（默认: KNF）"
    )
    parser.add_argument("--threads", type=int, default=10, help="线程数（默认: 4）")
    parser.add_argument("--generate_cnf", action="store_true", help="同时生成 CNF 文件")

    args = parser.parse_args()

    main(args.base_dir, args.output_dir, args.threads, args.generate_cnf)
