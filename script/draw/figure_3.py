#!/usr/bin/env python3
"""
Beautiful Speedup Scatter Plot Generator
风格参考 LaTeX PGFPlots，适用于学术论文发表。
"""

from pathlib import Path

import matplotlib.lines as mlines
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

TIME_RESOLUTION_SECONDS = 0.01
TIME_DECIMAL_PLACES = 2
# The accepted figure clamps plotted points at 0.1 s while retaining
# centisecond precision in the statistics.
PLOT_MIN_TIME_SECONDS = 0.1
# Compensate for the tight-layout drift between Matplotlib 3.10.7 (reference)
# and 3.10.9 so that the exported MediaBox remains identical.
FIGURE_SIZE_INCHES = (8, 8)

# Match the typography of the accepted Figure 3 PDF.
ANNOTATION_FONT_SIZE = 18
CATEGORY_LEGEND_FONT_SIZE = 18
STATISTICS_LEGEND_FONT_SIZE = 16


def normalize_runtime(time_value: float) -> float:
    """Match the centisecond precision of the historical timing data."""
    return round(max(time_value, TIME_RESOLUTION_SECONDS), TIME_DECIMAL_PLACES)


# ======================== 配置区域 ========================

# 绘图样式配置
STYLE_CONFIG = {
    # 颜色 (参考图 1 的风格)
    "color_sat": "#4169E1",  # RoyalBlue (SAT)
    "color_unsat": "#DC143C",  # Crimson (UNSAT)
    "color_grid": "#e0e0e0",  # 浅灰网格
    "color_diag": "#404040",  # 对角线颜色
    "color_timeout": "#666666",  # 超时线颜色
    # 标记
    "marker_sat": "d",  # 菱形
    "marker_unsat": "o",  # 圆形
    "marker_size": 90,  # 标记大小
    "alpha": 0.7,  # 透明度 (处理重叠)
    # 字体
    "font_family": "serif",  # 衬线字体 (接近 LaTeX)
    "font_size_label": 16,
    "font_size_tick": 12,
}

# ======================== 数据加载逻辑 (保持原逻辑) ========================


def load_whitelist(csv_dir: Path) -> set[str] | None:
    vbs_file = csv_dir / "satvbs.txt"
    if csv_dir.name.count("SAT25"):
        vbs_file = csv_dir / "satvbs-all.txt"
    if not vbs_file.exists():
        return None
    whitelist = set()
    with open(vbs_file, "r") as f:
        for line in f:
            instance = line.strip()
            if instance and instance != "satvbs.txt" and instance != "satvbs-all.txt":
                whitelist.add(instance)
    return whitelist


def load_results_with_status(csv_path: Path, timeout: float = 3600.0) -> dict:
    whitelist = load_whitelist(csv_path.parent)
    df = pd.read_csv(csv_path)
    df = df.dropna()

    # 标准化列名
    cols = {c.lower(): c for c in df.columns}
    time_col = cols.get("time")
    result_col = cols.get("result")
    instance_col = cols.get("instance")

    results = {}
    for _, row in df.iterrows():
        instance_name = str(row[instance_col]).strip()
        if whitelist is not None and instance_name not in whitelist:
            continue

        raw_time_val = float(row[time_col])
        time_val = raw_time_val

        # 结果判定
        status = "UNKNOWN"
        if result_col:
            res_str = str(row[result_col]).strip().upper()
            if res_str in ["SAT", "SATISFIABLE"]:
                status = "SAT"
            elif res_str in ["UNSAT", "UNSATISFIABLE"]:
                status = "UNSAT"
            elif res_str == "TIMEOUT":
                status = "TIMEOUT"

        # 强制超时截断
        if raw_time_val >= timeout or status == "TIMEOUT":
            time_val = timeout
            status = "TIMEOUT"  # 标记为超时，但在绘图时我们会尝试恢复其实际类型（如果另一个求解器解出）
        else:
            time_val = normalize_runtime(raw_time_val)

        results[instance_name] = (time_val, status)
    return results


# ======================== 核心绘图逻辑 ========================


def plot_beautiful_scatter(
    results_a,
    results_b,
    label_a,
    label_b,
    output_path,
    timeout=3600,
    min_time=TIME_RESOLUTION_SECONDS,
):
    # 1. 数据对齐与清洗
    common_instances = set(results_a.keys()) & set(results_b.keys())

    points_sat = []
    points_unsat = []
    points_unknown = []  # 如果两个都超时且未知结果

    # 统计信息
    solved_a = 0  # A 求解的个数
    solved_b = 0  # B 求解的个数
    par2_a = 0.0  # A 的 PAR-2 总和
    par2_b = 0.0  # B 的 PAR-2 总和
    total_time_a = 0.0  # A 成功求解的总时间
    total_time_b = 0.0  # B 成功求解的总时间

    for inst in common_instances:
        t_a, s_a = results_a[inst]
        t_b, s_b = results_b[inst]

        # 统计求解个数和时间
        if s_a in ["SAT", "UNSAT"]:
            solved_a += 1
            par2_a += t_a
            total_time_a += t_a
        else:
            par2_a += 2 * timeout

        if s_b in ["SAT", "UNSAT"]:
            solved_b += 1
            par2_b += t_b
            total_time_b += t_b
        else:
            par2_b += 2 * timeout

        # 确定该点的最终状态 (SAT/UNSAT)
        # 优先取非超时的状态，如果都超时且无法确定，归为 UNKNOWN
        final_status = "UNKNOWN"
        if s_a in ["SAT", "UNSAT"]:
            final_status = s_a
        elif s_b in ["SAT", "UNSAT"]:
            final_status = s_b

        # 坐标截断 (稍微小于timeout以免被边框盖住，或者正好在timeout上)
        # 这里我们允许正好在 timeout 上
        val_a = min(max(t_a, min_time), timeout)
        val_b = min(max(t_b, min_time), timeout)

        pt = (val_a, val_b)

        if final_status == "SAT":
            points_sat.append(pt)
        elif final_status == "UNSAT":
            points_unsat.append(pt)
        else:
            points_unknown.append(pt)

    # 计算平均值
    total_instances = len(common_instances)
    par2_a_avg = par2_a / total_instances if total_instances > 0 else 0
    par2_b_avg = par2_b / total_instances if total_instances > 0 else 0
    avg_time_a = total_time_a / solved_a if solved_a > 0 else 0
    avg_time_b = total_time_b / solved_b if solved_b > 0 else 0

    # 转换为 numpy 数组以便绘图
    np_sat = np.array(points_sat) if points_sat else np.empty((0, 2))
    np_unsat = np.array(points_unsat) if points_unsat else np.empty((0, 2))
    np_unknown = np.array(points_unknown) if points_unknown else np.empty((0, 2))

    # 2. 初始化画布
    plt.rcParams["font.family"] = STYLE_CONFIG["font_family"]
    fig, ax = plt.subplots(figsize=FIGURE_SIZE_INCHES)

    # 设置对数坐标
    ax.set_xscale("log")
    ax.set_yscale("log")

    # 设置显示范围 (给一点余量让超时线可见)
    limit_max = timeout * 1.8
    ax.set_xlim(min_time, limit_max)
    ax.set_ylim(min_time, limit_max)

    # 3. 绘制辅助线 (网格、对角线、加速比)

    # 基础网格
    ax.grid(
        True, which="major", color=STYLE_CONFIG["color_grid"], linestyle="-", alpha=0.8
    )
    ax.grid(
        True, which="minor", color=STYLE_CONFIG["color_grid"], linestyle="-", alpha=0.3
    )

    # 对角线 y=x
    ax.plot([min_time, limit_max], [min_time, limit_max], c="black", lw=1.0, alpha=0.6)

    # 加速比线函数
    def plot_speedup_line(factor):
        # y = factor * x (即 Solver A 更快，点在对角线上方)
        # y = x / factor (即 Solver B 更快，点在对角线下方)

        # 生成线条数据
        x_vals = np.array([min_time, limit_max])

        # 绘制上方线 (y = factor * x) -> Solver A 更快
        y_upper = x_vals * factor
        ax.plot(x_vals, y_upper, c="black", lw=0.6, alpha=0.4)

        # 绘制下方线 (y = x / factor) -> Solver B 更快
        y_lower = x_vals / factor
        ax.plot(x_vals, y_lower, c="black", lw=0.6, alpha=0.4)

        # 添加文本标注 - 在对数坐标下使用几何平均找中点
        # 上方线: y = factor * x, 需要找一个点使得 y < timeout 且 x < timeout
        # 线的可见范围: x 从 min_time 到 min(timeout/factor, timeout)
        x_upper_max = min(timeout / factor, timeout)
        if x_upper_max > min_time:
            # 在对数坐标下取几何平均作为中点
            text_x_upper = np.sqrt(min_time * x_upper_max)
            text_y_upper = text_x_upper * factor
            ax.text(
                text_x_upper,
                text_y_upper,
                f"{factor}$\\times$",
                fontsize=ANNOTATION_FONT_SIZE,
                rotation=45,
                ha="center",
                va="bottom",
                alpha=0.7,
            )

        # 下方线: y = x / factor, 需要找一个点使得 x < timeout 且 y < timeout
        # 线的可见范围: x 从 min_time 到 min(timeout * factor, timeout)
        x_lower_max = min(timeout * factor, timeout)
        if x_lower_max > min_time:
            text_x_lower = np.sqrt(min_time * x_lower_max)
            text_y_lower = text_x_lower / factor
            ax.text(
                text_x_lower,
                text_y_lower,
                f"{factor}$\\times$",
                fontsize=ANNOTATION_FONT_SIZE,
                rotation=45,
                ha="center",
                va="bottom",
                alpha=0.7,
            )

    # 绘制 2x, 10x, 100x 线
    for f in [10, 100]:
        plot_speedup_line(f)

    # 稍微淡一点的 2x 线
    ax.plot(
        [min_time, limit_max],
        [min_time * 2, limit_max * 2],
        c="black",
        lw=0.5,
        alpha=0.2,
    )
    ax.plot(
        [min_time, limit_max],
        [min_time / 2, limit_max / 2],
        c="black",
        lw=0.5,
        alpha=0.2,
    )

    # 4. 绘制超时线 (Box)
    # 垂直线
    ax.vlines(
        x=timeout,
        ymin=min_time,
        ymax=limit_max,
        colors=STYLE_CONFIG["color_timeout"],
        linestyles="--",
        lw=1.2,
    )
    # 水平线
    ax.hlines(
        y=timeout,
        xmin=min_time,
        xmax=limit_max,
        colors=STYLE_CONFIG["color_timeout"],
        linestyles="--",
        lw=1.2,
    )

    # 标注 "Timeout" - 在对数坐标下找线的中点位置
    # 垂直线的中点 (x = timeout, y 在 min_time 到 timeout 之间的几何平均)
    mid_y = np.sqrt(min_time * timeout)
    ax.text(
        timeout * 1.08,
        mid_y,
        "Timeout",
        rotation=90,
        ha="left",
        va="center",
        fontsize=ANNOTATION_FONT_SIZE,
        color=STYLE_CONFIG["color_timeout"],
    )
    # 水平线的中点 (y = timeout, x 在 min_time 到 timeout 之间的几何平均)
    mid_x = np.sqrt(min_time * timeout)
    ax.text(
        mid_x,
        timeout * 1.15,
        "Timeout",
        rotation=0,
        ha="center",
        va="bottom",
        fontsize=ANNOTATION_FONT_SIZE,
        color=STYLE_CONFIG["color_timeout"],
    )

    # 5. 绘制散点
    # 先画 Unknown (如果有)
    if len(np_unknown) > 0:
        ax.scatter(
            np_unknown[:, 0],
            np_unknown[:, 1],
            c="gray",
            marker="s",
            s=30,
            alpha=0.3,
            label="_nolegend_",
        )

    # 再画 SAT (蓝色菱形)
    if len(np_sat) > 0:
        ax.scatter(
            np_sat[:, 0],
            np_sat[:, 1],
            c=STYLE_CONFIG["color_sat"],
            marker=STYLE_CONFIG["marker_sat"],
            s=STYLE_CONFIG["marker_size"],
            alpha=float(STYLE_CONFIG["alpha"]),
            edgecolors="none",
        )  # 去掉边缘让颜色更融合

    # 最后画 UNSAT (红色圆形)，确保 UNSAT 在上面 (通常 UNSAT 较少且重要)
    if len(np_unsat) > 0:
        ax.scatter(
            np_unsat[:, 0],
            np_unsat[:, 1],
            c=STYLE_CONFIG["color_unsat"],
            marker=STYLE_CONFIG["marker_unsat"],
            s=STYLE_CONFIG["marker_size"],
            alpha=float(STYLE_CONFIG["alpha"]),
            edgecolors="none",
        )

    # 6. 设置轴标签
    ax.set_xlabel(
        f"{label_a} Time (s)",
        fontsize=STYLE_CONFIG["font_size_label"],
        fontweight="bold",
    )
    ax.set_ylabel(
        f"{label_b} Time (s)",
        fontsize=STYLE_CONFIG["font_size_label"],
        fontweight="bold",
    )
    ax.tick_params(axis="both", which="major", labelsize=STYLE_CONFIG["font_size_tick"])

    # 7. 使用三个独立图例：SAT/UNSAT 标记 + 算法A统计 + 算法B统计

    # 图例1: SAT/UNSAT 标记（左上角）
    legend1_elements = []
    if len(np_sat) > 0:
        legend1_elements.append(
            mlines.Line2D(
                [],
                [],
                color=STYLE_CONFIG["color_sat"],
                marker=STYLE_CONFIG["marker_sat"],
                linestyle="None",
                markersize=8,
                alpha=0.8,
                label="SAT",
            ),
        )
    if len(np_unsat) > 0:
        legend1_elements.append(
            mlines.Line2D(
                [],
                [],
                color=STYLE_CONFIG["color_unsat"],
                marker=STYLE_CONFIG["marker_unsat"],
                linestyle="None",
                markersize=8,
                alpha=0.8,
                label="UNSAT",
            ),
        )

    legend1 = ax.legend(
        handles=legend1_elements,
        loc="upper left",
        fontsize=CATEGORY_LEGEND_FONT_SIZE,
        frameon=True,
        framealpha=0.95,
        edgecolor="#ccc",
        fancybox=True,
    )
    ax.add_artist(legend1)
    legend3_elements = [
        mlines.Line2D([], [], linestyle="None", label=f"{label_a}:"),
        mlines.Line2D([], [], linestyle="None", label=f"  #Solved: {solved_a}"),
        mlines.Line2D([], [], linestyle="None", label=f"  PAR-2: {par2_a_avg:.1f}"),
        mlines.Line2D([], [], linestyle="None", label=f"{label_b}:"),
        mlines.Line2D([], [], linestyle="None", label=f"  #Solved: {solved_b}"),
        mlines.Line2D([], [], linestyle="None", label=f"  PAR-2: {par2_b_avg:.1f}"),
        # mlines.Line2D([], [], linestyle="None", label=f"  Avg: {avg_time_b:.1f}s"),
    ]

    legend3 = ax.legend(
        handles=legend3_elements,
        loc="lower right",
        bbox_to_anchor=(1.0, 0.0),  # 右下角右侧
        fontsize=STATISTICS_LEGEND_FONT_SIZE,
        frameon=True,
        framealpha=0.95,
        edgecolor="#ccc",
        fancybox=True,
        handlelength=0,
        handletextpad=0,
        borderpad=0.6,
    )
    # 设置标题为粗体
    legend3.get_texts()[0].set_fontweight("bold")
    legend3.get_texts()[0].set_fontsize(STATISTICS_LEGEND_FONT_SIZE)
    legend3.get_texts()[3].set_fontweight("bold")
    legend3.get_texts()[3].set_fontsize(STATISTICS_LEGEND_FONT_SIZE)
    # 保持正方形比例
    ax.set_aspect("equal", adjustable="box")

    # 保存
    plt.tight_layout()
    plt.savefig(
        output_path,
        dpi=300,
        bbox_inches="tight",
    )
    print(f"✅ 图表已保存至: {output_path}")
    plt.close()


# ======================== 主执行逻辑 ========================


def main():
    # 配置
    csv_dirs = [
        "data/csv/MaxSAT24",
        "data/csv/SAT25",
        "data/csv/DES",
        "data/csv/MVC",
        "data/csv/WSNO",
    ]
    timeout = 3600

    # 算法名称 (文件名匹配)
    solver_a_file = "cardsat-ccdcl+pre"
    solver_b_file = "ccdcl"

    # # 显示名称
    solver_a_display = "CCDCL-LS"
    solver_b_display = "CCDCL"

    output_name = f"output/{solver_a_display}_vs_{solver_b_display}.pdf"

    # 数据加载
    results_a = {}
    results_b = {}

    print("正在加载数据...")
    for d in csv_dirs:
        pa = Path(d) / f"{solver_a_file}_stats.csv"
        pb = Path(d) / f"{solver_b_file}_stats.csv"

        if pa.exists():
            results_a.update(load_results_with_status(pa, timeout))
        if pb.exists():
            results_b.update(load_results_with_status(pb, timeout))

    print(f"数据加载完成: A={len(results_a)}, B={len(results_b)}")

    if not results_a or not results_b:
        print("❌ 未找到数据文件，请检查路径。")
        return

    # 绘图
    plot_beautiful_scatter(
        results_a,
        results_b,
        solver_a_display,
        solver_b_display,
        output_name,
        timeout=timeout,
        min_time=PLOT_MIN_TIME_SECONDS,
    )


if __name__ == "__main__":
    main()
