"""
生成 LaTeX 表格格式的统计表格

包含两个函数：
1. generate_latex_table: 生成三线表（按实例集）
   - 第一列：实例集名称（带实例个数）
   - 每个求解器占两列：#Solved | PAR-2
   - 表头显示求解器类别和完备性

2. generate_latex_table_by_solver: 生成表格（按求解器分类）
   - 第一列：求解器名称（按完备性先分类，然后按类别）
   - 每个benchmark占两列：#Solved | PAR-2
   - 表头显示benchmark名称和实例个数
   - 使用横线分割不同类
"""

import os

import pandas as pd

TIMEOUT = 3600
PAR_MULTIPLIER = 2
TIME_RESOLUTION_SECONDS = 0.01
TIME_DECIMAL_PLACES = 2


def normalize_runtime(time_value: float) -> float:
    """Match the centisecond precision of the historical timing data."""
    return round(
        max(float(time_value), TIME_RESOLUTION_SECONDS), TIME_DECIMAL_PLACES
    )

# 求解器类别映射
SOLVER_CATEGORIES = {
    "cardsat-s20+backbone+pre+swap": "SAT",
    "ccanr": "SAT",
    "walksatlm": "SAT",
    "tassat": "SAT",
    "ccdcl": "SAT",
    "yalsat": "SAT",
    "roundingsat": "PB/PBO",
    "nupbo": "PB/PBO",
    "nupbo-deepopt+": "PB/PBO",
    "dls-pbo": "PB/PBO",
    "open-wbo": "MaxSAT",
}

# 求解器重命名映射（用于表格显示）
SOLVER_DISPLAY_NAMES = {
    "cardsat-s20+backbone+pre+swap": "CardSAT-LS",
    "ccdcl": "CCDCL",
    "roundingsat": "RoundingSAT",
    "nupbo": "NuPBO",
    "nupbo-deepopt+": "NuPBO-DeepOpt+",
    "ccanr": "CCAnr",
    "walksatlm": "WalkSATlm",
    "dls-pbo": "DLS-PBO",
    "yalsat": "YalSAT",
    "tassat": "TaSSAT",
    "open-wbo": "Open-WBO",
}

# 求解器完备性映射（Complete: 完备, Incomplete: 非完备）
SOLVER_COMPLETENESS = {
    "cardsat-s20+backbone+pre+swap": "Incomplete",
    "ccdcl": "Complete",
    "ccanr": "Incomplete",
    "walksatlm": "Incomplete",
    "yalsat": "Incomplete",
    "roundingsat": "Complete",
    "nupbo": "Incomplete",
    "nupbo-deepopt+": "Incomplete",
    "dls-pbo": "Incomplete",
    "open-wbo": "Complete",
    "tassat": "Incomplete",
}

# 类别显示顺序（按列表顺序显示，未列出的类别会排在最后）
CATEGORY_ORDER = [
    "SAT",
    "MaxSAT",
    "PB/PBO",
]


class Benchmark:
    def __init__(self, name: str):
        self.name = name
        self.results = {}
        self.vbs_instances = []

    def load_solver(self, solver: str):
        df = pd.read_csv(f"data/csv/{self.name}/{solver}_stats.csv")
        self.results[solver] = df

    def load_vbs_whitelist(self):
        vbs_file = f"data/csv/{self.name}/satvbs.txt"
        if not os.path.exists(vbs_file):
            return True
        with open(vbs_file, "r") as f:
            self.vbs_instances = [
                line.strip()
                for line in f
                if line.strip() and line.strip() != "satvbs.txt"
            ]
        return False

    def filter_by_whitelist(self, df: pd.DataFrame) -> pd.DataFrame:
        filtered_df = df[df["Instance"].isin(self.vbs_instances)].copy()

        existing_instances = set(filtered_df["Instance"])
        missing_instances = [
            instance
            for instance in self.vbs_instances
            if instance not in existing_instances
        ]

        if missing_instances:
            missing_df = pd.DataFrame(
                {
                    "Instance": missing_instances,
                    "Time": [TIMEOUT] * len(missing_instances),
                    "Result": ["unknown"] * len(missing_instances),
                }
            )
            for col in df.columns:
                if col not in missing_df.columns:
                    missing_df[col] = pd.NA
            missing_df = missing_df[df.columns]
            filtered_df = pd.concat([filtered_df, missing_df], ignore_index=True)

        order_map = {instance: idx for idx, instance in enumerate(self.vbs_instances)}
        filtered_df["_order"] = filtered_df["Instance"].map(order_map)
        filtered_df = (
            filtered_df.sort_values("_order")
            .drop(columns=["_order"])
            .reset_index(drop=True)
        )
        return filtered_df

    def compute_vbs(self):
        all_data = []
        for solver, df in self.results.items():
            filtered_df = self.filter_by_whitelist(df)
            for _, row in filtered_df.iterrows():
                all_data.append(
                    {
                        "Instance": row["Instance"],
                        "Solver": solver,
                        "Time": row["Time"],
                        "Result": row["Result"],
                    }
                )

        all_df = pd.DataFrame(all_data)

        vbs_results = []
        for instance in self.vbs_instances:
            instance_data = all_df[all_df["Instance"] == instance]
            if len(instance_data) == 0:
                continue

            best_idx = instance_data["Time"].idxmin()
            best_row = instance_data.loc[best_idx]

            # solved = (instance_data["Result"] == "sat").any()
            solved = (instance_data["Result"] != "unknown").any()
            vbs_results.append(
                {
                    "Instance": instance,
                    "Time": best_row["Time"],
                    "Result": best_row["Result"] if solved else "unknown",
                }
            )

        self.results["VBS"] = pd.DataFrame(vbs_results)

    def compute_stats(self, solver: str) -> dict:
        df = self.results.get(solver)

        if df is None:
            return None

        if solver != "VBS":
            df = self.filter_by_whitelist(df)

        # solved_df = df[df["Result"] == "sat"]
        solved_df = df[df["Result"] != "unknown"]
        num_solved = len(solved_df)

        par_times = []
        for _, row in df.iterrows():
            if row["Result"] != "unknown":
                par_times.append(normalize_runtime(row["Time"]))
            else:
                par_times.append(TIMEOUT * PAR_MULTIPLIER)

        par_x = sum(par_times) / len(par_times) if par_times else float("nan")

        return {
            "Solver": solver,
            "#Solved": num_solved,
            "PAR2": par_x,
        }

    def get_all_stats(self, solvers: list) -> dict:
        stats = {}
        for solver in solvers:
            stat = self.compute_stats(solver)
            if stat:
                stats[solver] = stat
        vbs_stat = self.compute_stats("VBS")
        if vbs_stat:
            stats["VBS"] = vbs_stat
        return stats


def format_number(value, is_int=False):
    """格式化数字，处理 NaN"""
    if pd.isna(value):
        return "---"
    if is_int:
        return str(int(value))
    return f"{value:.2f}"


def escape_latex(text: str) -> str:
    """转义 LaTeX 特殊字符"""
    text = text.replace("\\", "\\textbackslash{}")
    text = text.replace("&", "\\&")
    text = text.replace("%", "\\%")
    text = text.replace("$", "\\$")
    text = text.replace("#", "\\#")
    text = text.replace("^", "\\textasciicircum{}")
    text = text.replace("_", "\\_")
    text = text.replace("{", "\\{")
    text = text.replace("}", "\\}")
    text = text.replace("~", "\\textasciitilde{}")
    return text


def sort_categories(categories: dict) -> list:
    """按照 CATEGORY_ORDER 列表的顺序排序类别，未列出的类别排在最后"""
    ordered_categories = []
    for category in CATEGORY_ORDER:
        if category in categories:
            ordered_categories.append(category)
    other_categories = sorted(
        [cat for cat in categories.keys() if cat not in CATEGORY_ORDER]
    )
    ordered_categories.extend(other_categories)
    return ordered_categories


def generate_latex_table_by_solver(benchmarks_data: dict, solvers: list):
    """生成 LaTeX 表格（按求解器分类）
    表格结构：
    - 第一列：求解器名称（按完备性先分类，然后按类别）
    - 每个benchmark占两列：#Solved | PAR-2
    - 表头：显示benchmark名称和实例个数
    - 使用横线分割不同类
    """
    if not benchmarks_data:
        return ""

    # 获取所有benchmark名称（按字母顺序排序）
    benchmark_names = benchmarks_data.keys()
    total_benchmarks = len(benchmark_names)
    if total_benchmarks == 0:
        return ""

    # 按完备性先分组，然后按类别分组
    completeness_groups = {}
    for solver in solvers:
        completeness = SOLVER_COMPLETENESS.get(solver, "Unknown")
        category = SOLVER_CATEGORIES.get(solver, "Other")

        if completeness not in completeness_groups:
            completeness_groups[completeness] = {}
        if category not in completeness_groups[completeness]:
            completeness_groups[completeness][category] = []
        completeness_groups[completeness][category].append(solver)

    # 添加 VBS
    if any("VBS" in data for data in benchmarks_data.values()):
        if "VBS" not in completeness_groups:
            completeness_groups["VBS"] = {}
        completeness_groups["VBS"]["VBS"] = ["VBS"]

    # 按完备性顺序排序（Complete, Incomplete, VBS, Unknown）
    completeness_order = ["Complete", "Incomplete", "VBS", "Unknown"]
    ordered_completeness = []
    for comp in completeness_order:
        if comp in completeness_groups:
            ordered_completeness.append(comp)
    other_completeness = sorted(
        [c for c in completeness_groups.keys() if c not in completeness_order]
    )
    ordered_completeness.extend(other_completeness)

    # 收集所有求解器（按完备性、类别、求解器顺序）
    all_solvers_ordered = []
    solver_group_info = []  # 记录每个组的起始和结束位置，用于添加横线

    for completeness in ordered_completeness:
        categories_in_completeness = completeness_groups[completeness]
        ordered_categories = sort_categories(categories_in_completeness)

        for category in ordered_categories:
            solvers_in_group = sorted(categories_in_completeness[category])
            if solvers_in_group:
                start_idx = len(all_solvers_ordered)
                all_solvers_ordered.extend(solvers_in_group)
                end_idx = len(all_solvers_ordered) - 1
                solver_group_info.append(
                    {
                        "completeness": completeness,
                        "category": category,
                        "start": start_idx,
                        "end": end_idx,
                    }
                )

    total_solvers = len(all_solvers_ordered)
    if total_solvers == 0:
        return ""

    # 生成表头
    latex_lines = []
    latex_lines.append("\\begin{table*}[htbp]")
    latex_lines.append("\\centering")
    latex_lines.append("\\setlength{\\tabcolsep}{3pt}")
    latex_lines.append("\\caption{All Solvers}")
    latex_lines.append("\\label{tab:solver-stats}")

    # 列格式：1列求解器名称 + 每个benchmark 2列（#Solved, PAR-2） + 最后2列 Total
    latex_lines.append("\\renewcommand{\\arraystretch}{1.3}")  # 增加行间距
    latex_lines.append("\\begin{tabular}{l" + "cc" * total_benchmarks + "cc}")
    latex_lines.append("\\hline")

    # 计算所有 benchmark 的总实例数
    total_instances = sum(
        len(benchmarks_data[b].get("instances", [])) for b in benchmark_names
    )

    # 第一行：benchmark名称行（包含实例个数）
    benchmark_row = "\\textbf{ }"
    for benchmark_name in benchmark_names:
        num_instances = len(benchmarks_data[benchmark_name].get("instances", []))
        benchmark_display = escape_latex(benchmark_name)
        benchmark_row += f" & \\multicolumn{{2}}{{c}}{{\\textbf{{{benchmark_display}}} ({num_instances})}}"
    # 添加 Total 列
    benchmark_row += (
        f" & \\multicolumn{{2}}{{c}}{{\\textbf{{Total}} ({total_instances})}}"
    )
    benchmark_row += " \\\\"
    latex_lines.append(benchmark_row)

    # 第二行：指标名称行（#Solved 和 PAR-2）
    metric_row = "\\textbf{ }"
    for benchmark_name in benchmark_names:
        metric_row += " & \\textit{\\#Solved} & \\textit{PAR-2}"
    # 添加 Total 列的指标名称
    metric_row += " & \\textit{\\#Solved} & \\textit{PAR-2}"
    metric_row += " \\\\"
    latex_lines.append(metric_row)
    latex_lines.append("\\hline")

    # 生成数据行（每个求解器一行）
    # 创建一个映射，从求解器索引到组信息
    solver_to_group = {}
    for group_info in solver_group_info:
        for idx in range(group_info["start"], group_info["end"] + 1):
            solver_to_group[idx] = group_info

    previous_group = None

    for idx, solver in enumerate(all_solvers_ordered):
        current_group = solver_to_group.get(idx)

        # 检查是否需要添加分隔线（组改变时，除了第一个组）
        if previous_group is not None and current_group is not None:
            if (
                current_group["completeness"] != previous_group["completeness"]
                or current_group["category"] != previous_group["category"]
            ):
                latex_lines.append("\\hline")

        previous_group = current_group

        # 生成求解器显示名称
        solver_display = SOLVER_DISPLAY_NAMES.get(solver, solver)
        solver_display = escape_latex(solver_display)

        row = f"{{\\sc {solver_display}}}"

        # 用于计算 Total 的累加变量
        total_solved = 0
        total_par2_sum = 0.0  # PAR2 * 实例数 的累加
        total_instance_count = 0
        has_any_data = False

        for benchmark_name in benchmark_names:
            stats = benchmarks_data[benchmark_name]
            num_instances = len(stats.get("instances", []))

            if solver in stats:
                solver_stat = stats[solver]
                solved = solver_stat["#Solved"]
                par2 = solver_stat["PAR2"]

                row += " & " + format_number(solved, is_int=True)
                row += " & " + format_number(par2)

                # 累加 Total 数据
                if not pd.isna(solved):
                    total_solved += int(solved)
                    has_any_data = True
                if not pd.isna(par2) and num_instances > 0:
                    total_par2_sum += par2 * num_instances
                    total_instance_count += num_instances
            else:
                row += " & --- & ---"

        # 添加 Total 列
        if has_any_data:
            row += " & " + format_number(total_solved, is_int=True)
            # 计算加权平均 PAR-2
            if total_instance_count > 0:
                total_par2_avg = total_par2_sum / total_instance_count
                row += " & " + format_number(total_par2_avg)
            else:
                row += " & ---"
        else:
            row += " & --- & ---"

        row += " \\\\"
        latex_lines.append(row)

    latex_lines.append("\\hline")
    latex_lines.append("\\end{tabular}")
    latex_lines.append("\\end{table*}")
    latex_lines.append("")

    return "\n".join(latex_lines)


def generate_latex_table(benchmarks_data: dict, solvers: list):
    """生成 LaTeX 三线表
    表格结构：
    - 第一列：实例集名称（带实例个数）
    - 每个求解器占两列：#Solved | PAR-2
    - 表头：按类别分组显示求解器
    """
    if not benchmarks_data:
        return ""

    # 按类别和完备性分组求解器
    categories = {}
    for solver in solvers:
        category = SOLVER_CATEGORIES.get(solver, "Other")
        completeness = SOLVER_COMPLETENESS.get(solver, "Unknown")
        if category not in categories:
            categories[category] = {}
        if completeness not in categories[category]:
            categories[category][completeness] = []
        categories[category][completeness].append(solver)

    # 添加 VBS
    if any("VBS" in data for data in benchmarks_data.values()):
        if "VBS" not in categories:
            categories["VBS"] = {}
        categories["VBS"]["VBS"] = ["VBS"]

    # 收集所有求解器（按类别和完备性排序）
    ordered_categories = sort_categories(categories)
    all_solvers_ordered = []
    category_completeness_info = {}

    for category in ordered_categories:
        category_completeness_info[category] = {}
        if category == "VBS":
            for completeness in ["VBS"]:
                if completeness in categories[category]:
                    solvers_in_group = categories[category][completeness]
                    category_completeness_info[category][completeness] = len(
                        solvers_in_group
                    )
                    all_solvers_ordered.extend(solvers_in_group)
        else:
            for completeness in ["Complete", "Incomplete", "Unknown"]:
                if completeness in categories[category]:
                    solvers_in_group = categories[category][completeness]
                    category_completeness_info[category][completeness] = len(
                        solvers_in_group
                    )
                    all_solvers_ordered.extend(solvers_in_group)

    total_solvers = len(all_solvers_ordered)
    if total_solvers == 0:
        return ""

    # 生成表头
    latex_lines = []
    latex_lines.append("\\begin{table*}[htbp]")
    latex_lines.append("\\centering")
    latex_lines.append("\\tiny")
    latex_lines.append("\\setlength{\\tabcolsep}{3pt}")  # 减小列间距
    latex_lines.append("\\caption{All Benchmarks}")
    latex_lines.append("\\label{tab:benchmark-stats}")

    # 列格式：1列实例集名称 + 每个求解器2列（#Solved, PAR-2）
    latex_lines.append("\\begin{tabular}{l" + "cc" * total_solvers + "}")
    latex_lines.append("\\toprule")

    # 第一行：类别行
    category_row = "\\multicolumn{1}{c}{\\textbf{Benchmark}}"
    for category in ordered_categories:
        num_solvers = sum(category_completeness_info[category].values())
        if num_solvers > 0:
            category_row += (
                f" & \\multicolumn{{{num_solvers * 2}}}{{c}}{{\\textbf{{{category}}}}}"
            )
    category_row += " \\\\"
    latex_lines.append(category_row)

    # 添加类别分隔线
    col_start = 2
    for category in ordered_categories:
        num_solvers = sum(category_completeness_info[category].values())
        if num_solvers > 0:
            col_end = col_start + num_solvers * 2 - 1
            latex_lines.append(f"\\cmidrule(lr){{{col_start}-{col_end}}}")
            col_start = col_end + 1

    # 第二行：完备/非完备子分类行（VBS 类别跳过）
    completeness_row = "\\multicolumn{1}{c}{}"
    for category in ordered_categories:
        if category == "VBS":
            num_solvers = sum(category_completeness_info[category].values())
            if num_solvers > 0:
                completeness_row += f" & \\multicolumn{{{num_solvers * 2}}}{{c}}{{}}"
        else:
            for completeness in ["Complete", "Incomplete", "Unknown"]:
                if completeness in category_completeness_info[category]:
                    num_solvers = category_completeness_info[category][completeness]
                    if num_solvers > 0:
                        completeness_row += f" & \\multicolumn{{{num_solvers * 2}}}{{c}}{{\\textit{{{completeness}}}}}"
    completeness_row += " \\\\"
    latex_lines.append(completeness_row)

    # 添加完备性分隔线（VBS 类别跳过）
    col_start = 2
    for category in ordered_categories:
        if category == "VBS":
            num_solvers = sum(category_completeness_info[category].values())
            col_start += num_solvers * 2
        else:
            for completeness in ["Complete", "Incomplete", "Unknown"]:
                if completeness in category_completeness_info[category]:
                    num_solvers = category_completeness_info[category][completeness]
                    if num_solvers > 0:
                        col_end = col_start + num_solvers * 2 - 1
                        latex_lines.append(f"\\cmidrule(lr){{{col_start}-{col_end}}}")
                        col_start = col_end + 1

    # 第三行：求解器名称行（每个求解器占两列）
    solver_row = "\\textbf{ }"
    for solver in all_solvers_ordered:
        solver_display = SOLVER_DISPLAY_NAMES.get(solver, solver)
        if len(solver_display) > 20:
            solver_display = solver_display[:17] + "..."
        solver_display = escape_latex(solver_display)
        solver_row += f" & \\multicolumn{{2}}{{c}}{{{{\\sc {solver_display}}}}}"
    solver_row += " \\\\"
    latex_lines.append(solver_row)

    # 第四行：指标名称行（#Solved 和 PAR-2）
    metric_row = "\\textbf{ }"
    for solver in all_solvers_ordered:
        metric_row += " & \\textit{\\#Solved} & \\textit{PAR-2}"
    metric_row += " \\\\"
    latex_lines.append(metric_row)
    latex_lines.append("\\midrule")

    # 生成数据行（每个实例集一行）
    for benchmark_name, stats in benchmarks_data.items():
        # 获取实例个数
        num_instances = len(stats.get("instances", []))
        benchmark_display = f"{escape_latex(benchmark_name)} ({num_instances})"

        row = f"\\textbf{{{benchmark_display}}}"
        for solver in all_solvers_ordered:
            if solver in stats:
                solver_stat = stats[solver]
                row += " & " + format_number(solver_stat["#Solved"], is_int=True)
                row += " & " + format_number(solver_stat["PAR2"])
            else:
                row += " & --- & ---"
        row += " \\\\"
        latex_lines.append(row)

    latex_lines.append("\\bottomrule")
    latex_lines.append("\\end{tabular}")
    latex_lines.append("\\end{table*}")
    latex_lines.append("")

    return "\n".join(latex_lines)


def main():
    benchmarks = ["MaxSAT24", "SAT25", "DES", "MVC", "WSNO"]
    solvers = [solver for solver in SOLVER_CATEGORIES.keys() if solver != "VBS"]

    benchmarks_data = {}

    for benchmark_name in benchmarks:
        benchmark = Benchmark(benchmark_name)

        skip = benchmark.load_vbs_whitelist()
        if skip:
            continue

        for solver in solvers:
            try:
                benchmark.load_solver(solver)
            except FileNotFoundError:
                print(f"Warning: {solver} results not found for {benchmark_name}")

        benchmark.compute_vbs()

        stats = benchmark.get_all_stats(solvers)
        # 保存实例列表
        stats["instances"] = benchmark.vbs_instances
        benchmarks_data[benchmark_name] = stats

    latex_table = generate_latex_table_by_solver(benchmarks_data, solvers)
    print(latex_table)


if __name__ == "__main__":
    main()
