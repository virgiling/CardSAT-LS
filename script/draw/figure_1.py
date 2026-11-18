#!/usr/bin/env python3

import os
from pathlib import Path
from typing import TypedDict

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

TIME_RESOLUTION_SECONDS = 0.01
TIME_DECIMAL_PLACES = 2
# Match the accepted camera-ready Figure 1 canvas and typography.
# Compensate for the tight-layout differences between Matplotlib 3.10.7
# (reference artifact) and 3.10.9 so the cropped PDF bounds stay identical.
FIGURE_SIZE_INCHES = (12, 10)
FIGURE_LAYOUT_DPI = 320
AXIS_LABEL_FONT_SIZE = 20
LEGEND_FONT_SIZE = 20
# Sub-point correction for Matplotlib 3.10.9's tight-layout bounds relative
# to the 3.10.7 reference PDF. This preserves the reference axes geometry.
AXES_WIDTH_TRIM_POINTS = 0.0871875
AXES_HEIGHT_TRIM_POINTS = 0.045


def normalize_runtime(time_value: float) -> float:
    """Match the centisecond precision of the historical timing data."""
    return round(max(time_value, TIME_RESOLUTION_SECONDS), TIME_DECIMAL_PLACES)


def solver_name_from_path(csv_path: Path) -> str:
    """Return the solver identifier encoded in a statistics CSV filename."""
    return csv_path.stem.replace("-results", "").replace("_stats", "")


def to_small_caps(text: str) -> str:
    """将文本转换为小型大写字母（使用 Unicode）"""
    # 小写字母 -> Unicode 小型大写字母映射
    normal = "abcdefghijklmnopqrstuvwxyz"
    small_caps = "ᴀʙᴄᴅᴇꜰɢʜɪᴊᴋʟᴍɴᴏᴘǫʀꜱᴛᴜᴠᴡxʏᴢ"
    trans = str.maketrans(normal, small_caps)
    return text.translate(trans)


def load_whitelist(csv_dir: Path) -> set[str] | None:
    """
    加载同目录下的 satvbs.txt 白名单文件

    Args:
        csv_dir: CSV 文件所在目录

    Returns:
        白名单实例集合，如果文件不存在则返回 None
    """
    vbs_file = csv_dir / "satvbs.txt"
    if not vbs_file.exists():
        return None

    whitelist = set()
    with open(vbs_file, "r") as f:
        for line in f:
            instance = line.strip()
            if instance and instance != "satvbs.txt":
                whitelist.add(instance)
    return whitelist


def load_results(
    csv_path: Path, timeout: float = 5000.0
) -> tuple[str, list[float], float, int]:
    """
    从CSV文件加载结果，返回算法名称、成功求解的时间列表、PAR-2总和和实例总数
    只加载在同目录 satvbs.txt 中列出的实例

    Args:
        csv_path: CSV文件路径
        timeout: 超时时间阈值

    Returns:
        (算法名称, 成功求解时间列表, PAR-2总和, 实例总数)
    """
    # 从文件名推断算法名称
    solver_name = solver_name_from_path(csv_path)

    # 加载白名单
    whitelist = load_whitelist(csv_path.parent)

    # 使用 pandas 读取 CSV；仅必需列中的空值会使该行无效。
    # Best/Mono 等可选列为空不应改变求解数或 PAR-2 分母。
    df = pd.read_csv(csv_path)

    # 检查列名（兼容大小写）
    time_col = "Time" if "Time" in df.columns else "time"
    result_col = "Result" if "Result" in df.columns else "result"
    instance_col = "Instance" if "Instance" in df.columns else "instance"
    required_columns = [time_col]
    if result_col in df.columns:
        required_columns.append(result_col)
    if instance_col in df.columns:
        required_columns.append(instance_col)
    df = df.dropna(subset=required_columns)

    times = []
    par2_sum = 0.0
    total_count = 0
    for _, row in df.iterrows():
        # 如果有白名单，检查实例是否在白名单中
        if whitelist is not None and instance_col in df.columns:
            instance_name = str(row[instance_col]).strip()
            if instance_name not in whitelist:
                continue

        raw_time_val = float(row[time_col])
        time_val = normalize_runtime(raw_time_val)
        total_count += 1

        # 如果有 result 列，只统计成功求解的实例（非 TIMEOUT）
        if result_col in df.columns:
            result = str(row[result_col]).strip()
            if result in ["sat", "SAT"]:
                times.append(time_val)
                par2_sum += time_val
            else:
                par2_sum += 2 * timeout
        # 如果没有 result 列，则使用时间阈值判断
        else:
            if raw_time_val < timeout:
                times.append(time_val)
                par2_sum += time_val
            else:
                par2_sum += 2 * timeout

    return solver_name, sorted(times), par2_sum, total_count


class SolverInfo(TypedDict):
    times: list[float]
    par2_sum: float
    total_count: int


def plot_cdf(
    results: dict[str, SolverInfo],
    output_path: str = "cdf.pdf",
    title: str = "CDF of Solving Time",
    timeout: float = 5000.0,
    log_scale: bool = True,
    name_mapping: dict[str, str] | None = None,
):
    """
    绘制 CDF 图

    Args:
        results: 字典 {算法名称: {times, par2_sum, total_count}}
        output_path: 输出文件路径
        title: 图表标题
        timeout: 超时时间
        log_scale: 是否使用对数坐标
        name_mapping: 自定义算法名称映射，如 {"ccanr": "CCAnr", "yalsat": "YalSAT"}
    """
    if name_mapping is None:
        name_mapping = {}

    # 设置图形样式
    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams["font.family"] = "serif"
    fig, ax = plt.subplots(figsize=FIGURE_SIZE_INCHES, dpi=FIGURE_LAYOUT_DPI)

    # 配色方案 - 学术论文风格，高对比度，支持 15+ 种颜色
    colors = [
        "#0072B2",  # 深蓝
        "#D55E00",  # 橙红
        "#009E73",  # 青绿
        "#CC79A7",  # 玫红
        "#F0E442",  # 金黄
        "#56B4E9",  # 天蓝
        "#E69F00",  # 橙黄
        "#000000",  # 黑色
        "#882255",  # 紫红
        "#44AA99",  # 蓝绿
        "#332288",  # 深紫
        "#117733",  # 深绿
        "#AA4499",  # 紫粉
        "#DDCC77",  # 土黄
        "#88CCEE",  # 浅蓝
    ]

    # Marker 样式 - 实心和空心交替，支持 15+ 种
    # 格式: (marker, fillstyle)
    marker_styles = [
        ("o", "full"),  # 实心圆
        ("s", "full"),  # 实心方
        ("^", "full"),  # 实心上三角
        ("D", "full"),  # 实心菱形
        ("v", "full"),  # 实心下三角
        ("o", "none"),  # 空心圆
        ("s", "none"),  # 空心方
        ("^", "none"),  # 空心上三角
        ("D", "none"),  # 空心菱形
        ("v", "none"),  # 空心下三角
        ("p", "full"),  # 实心五边形
        ("h", "full"),  # 实心六边形
        ("*", "full"),  # 实心星
        ("<", "full"),  # 实心左三角
        (">", "full"),  # 实心右三角
    ]

    plot_idx = 0
    for solver_name, solver_info in sorted(results.items()):
        # 获取显示名称
        display_name = name_mapping.get(solver_name, None)
        if display_name is None:
            continue

        # 获取样式
        color = colors[plot_idx % len(colors)]
        marker, fillstyle = marker_styles[plot_idx % len(marker_styles)]

        times = solver_info["times"]
        par2_sum = solver_info["par2_sum"]
        total_count = solver_info["total_count"]
        par2 = par2_sum / total_count if total_count else float("nan")

        if times:
            # 准备 CDF 数据
            sorted_times = np.array(sorted(times))
            n_solved = len(sorted_times)

            # CDF: y 值是累积数量
            y_values = np.arange(1, n_solved + 1)

            # 添加起始点 (最小时间点, 0)
            x_plot = np.concatenate([[sorted_times[0]], sorted_times])
            y_plot = np.concatenate([[0], y_values])
        else:
            # 无成功实例：画一条 y=0 的水平线，仍在图例中显示
            x_plot = np.array([max(0.001, timeout * 0.001), timeout])
            y_plot = np.array([0, 0])

        ax.step(
            x_plot,
            y_plot,
            where="post",
            label=display_name,
            color=color,
            linestyle="-",
            linewidth=0.9,
            alpha=0.85,
            marker=marker,
            markersize=5,
            markevery=4,
            fillstyle=fillstyle,
            markeredgewidth=0.8,
            markeredgecolor=color,
        )
        plot_idx += 1

    # 设置坐标轴
    if log_scale:
        ax.set_xscale("log")
        ax.set_xlabel("CPU Time (s)", fontsize=AXIS_LABEL_FONT_SIZE, fontweight="bold")
    else:
        ax.set_xlabel("CPU Time (s)", fontsize=AXIS_LABEL_FONT_SIZE, fontweight="bold")

    ax.set_ylabel(
        "Solved Instances (SAT)", fontsize=AXIS_LABEL_FONT_SIZE, fontweight="bold"
    )
    # ax.set_title(title, fontsize=16, fontweight="bold", pad=15)

    # 设置 x 轴范围
    all_times = [
        t for solver_info in results.values() for t in solver_info["times"] if t > 0
    ]
    if all_times:
        min_time = min(all_times) * 0.5
        ax.set_xlim(left=max(0.001, min_time), right=timeout * 1.1)

    # 添加图例 - 带方框和阴影
    legend = ax.legend(
        loc="upper left",
        fontsize=LEGEND_FONT_SIZE,
        ncol=2,  # 两列显示
        frameon=True,  # 显示边框
        fancybox=True,  # 圆角边框
        shadow=True,  # 阴影效果
        framealpha=0.95,
        edgecolor="black",
        columnspacing=1.0,  # 列间距
        handlelength=1.5,  # 图例句柄长度
    )
    legend.get_frame().set_linewidth(1.5)  # 边框粗细

    # 添加超时线
    ax.axvline(x=timeout, color="gray", linestyle="--", alpha=0.7, linewidth=1.5)
    ax.text(
        timeout * 0.85,
        ax.get_ylim()[1] * 0.02,
        f"Timeout={timeout}s",
        fontsize=14,
        color="gray",
        ha="right",
    )

    # 美化
    ax.tick_params(axis="both", labelsize=16)
    ax.grid(True, alpha=0.3, linestyle="-", linewidth=0.5)

    plt.tight_layout()
    axes_position = ax.get_position()
    ax.set_position(
        [
            axes_position.x0,
            axes_position.y0,
            axes_position.width - AXES_WIDTH_TRIM_POINTS / (72 * fig.get_figwidth()),
            axes_position.height - AXES_HEIGHT_TRIM_POINTS / (72 * fig.get_figheight()),
        ]
    )
    plt.savefig(output_path, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close()

    print(f"✅ CDF 图已保存到: {output_path}")


# 支持多个目录合并（同名文件数据会合并）
csv_dirs = [
    "data/csv/MaxSAT24",
    "data/csv/SAT25",
    "data/csv/DES",
    "data/csv/MVC",
    "data/csv/WSNO",
]

timeout = 3600
cdf_plot_name = "All-Bench-All-Solver"

# 输出目录使用第一个目录，或自定义
output_dir = "output"
os.makedirs(output_dir, exist_ok=True)

name_mapping = {
    "cardsat-s20+backbone+pre+swap": "CardSAT-LS",
    "dls-pbo": "DLS-PBO",
    "ccanr": "CCAnr",
    "tassat": "TaSSAT",
    "walksatlm": "WalkSATlm",
    "yalsat": "YalSAT",
    "nupbo": "NuPBO",
    "nupbo-deepopt+": "NuPBO-DeepOpt+",
    "open-wbo": "Open-WBO",
    "ccdcl": "CCDCL",
    "roundingsat": "RoundingSAT",
}

# 从多个目录加载并合并同名文件的数据
results = {}
for csv_dir in csv_dirs:
    # Analysis provenance/manifest CSVs use a different schema and may live
    # beside the paper data. Only solver statistics are valid plot inputs.
    csv_files = sorted(Path(csv_dir).glob("*_stats.csv"))
    for csv_path in csv_files:
        if solver_name_from_path(csv_path) not in name_mapping:
            continue
        solver_name, times, par2_sum, total_count = load_results(csv_path, timeout)
        if solver_name in results:
            # 合并同名求解器的数据
            results[solver_name]["times"].extend(times)
            results[solver_name]["par2_sum"] += par2_sum
            results[solver_name]["total_count"] += total_count
        else:
            results[solver_name] = {
                "times": times,
                "par2_sum": par2_sum,
                "total_count": total_count,
            }

# 对合并后的数据重新排序
print("=" * 60)
for solver_name in results:
    results[solver_name]["times"] = sorted(results[solver_name]["times"])
    print(f"solver: {solver_name}, #solved: {len(results[solver_name]['times'])}")
print("=" * 60)

plot_cdf(
    results,
    output_path=f"{output_dir}/{cdf_plot_name}.pdf",
    title=f"{cdf_plot_name}",
    timeout=timeout,
    log_scale=True,
    name_mapping=name_mapping,
)
