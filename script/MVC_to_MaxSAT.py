#!/usr/bin/env python
# -*- coding: UTF-8 -*-

"""
将最小顶点覆盖（MVC）问题编码为 MaxSAT 问题

输入：DIMACS 格式的图文件
输出：MaxSAT WCNF 格式文件
"""

import os
import sys
import csv
import argparse
import glob
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm


def parse_dimacs_graph(filename):
    """
    解析 DIMACS 格式的图文件

    格式：
    p edge <num_vertices> <num_edges>
    e <u> <v>

    返回：
    - num_vertices: 顶点数
    - edges: 边的列表，每个边是 (u, v) 元组
    """
    num_vertices = 0
    edges = []

    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("c"):
                continue

            if line.startswith("p"):
                parts = line.split()
                if len(parts) >= 4 and parts[1] == "edge":
                    num_vertices = int(parts[2])
                    _ = int(parts[3])

            elif line.startswith("e"):
                parts = line.split()
                if len(parts) >= 3:
                    u = int(parts[1])
                    v = int(parts[2])
                    edges.append((u, v))

    return num_vertices, edges


def encode_mvc_to_maxsat(num_vertices, edges, output_file):
    """
    将最小顶点覆盖问题编码为 MaxSAT 问题

    编码方式：
    - 为每个顶点 v 创建变量 x_v（表示 v 是否在覆盖中）
    - 对于每条边 (u,v)，硬约束：x_u OR x_v（至少一个端点在覆盖中）
    - 对于每个顶点 v，软约束：-x_v（权重为1，表示我们希望 v 不在覆盖中，即最小化覆盖大小）

    输出 WCNF 格式：
    p wcnf <num_vars> <num_clauses> <top_weight>
    <weight> <literals> 0
    """
    # 计算变量数和子句数
    num_vars = num_vertices
    num_hard_clauses = len(edges)  # 每条边一个硬约束
    num_soft_clauses = num_vertices  # 每个顶点一个软约束
    num_clauses = num_hard_clauses + num_soft_clauses

    # top_weight 应该大于所有软子句权重之和
    # 使用 num_vertices + 1 作为 top_weight（因为最多有 num_vertices 个软子句，每个权重为1）
    top_weight = num_vertices + 1

    with open(output_file, "w") as f:
        # 写入头部
        f.write(f"p wcnf {num_vars} {num_clauses} {top_weight}\n")

        # 写入硬约束：对于每条边 (u,v)，要求 x_u OR x_v
        for u, v in edges:
            f.write(f"{top_weight} {u} {v} 0\n")

        # 写入软约束：对于每个顶点 v，希望 -x_v（即 v 不在覆盖中）
        # 权重为1，表示最小化覆盖大小
        for v in range(1, num_vertices + 1):
            f.write(f"1 -{v} 0\n")


def process_single_file(input_file, output_dir):
    """
    处理单个文件的函数（用于并行处理）

    参数：
        input_file: 输入文件路径
        output_dir: 输出目录路径

    返回：
        (success, error_message)
    """
    try:
        # 检查输入文件是否存在
        if not os.path.exists(input_file):
            return (False, f"输入文件 {input_file} 不存在")

        # 解析图文件
        num_vertices, edges = parse_dimacs_graph(input_file)

        # 确定输出文件路径
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        if output_dir:
            output_file = os.path.join(output_dir, f"{base_name}.wcnf")
        else:
            output_dir_path = os.path.dirname(input_file)
            if output_dir_path:
                output_file = os.path.join(output_dir_path, f"{base_name}.wcnf")
            else:
                output_file = f"{base_name}.wcnf"

        # 确保输出目录存在
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir, exist_ok=True)

        # 编码为 MaxSAT
        encode_mvc_to_maxsat(num_vertices, edges, output_file)
        return (True, None)

    except Exception as e:
        return (False, str(e))


def collect_input_files(input_paths):
    """
    收集输入文件列表

    支持：
    - 单个文件或多个文件
    - 目录（处理所有 文件）
    - 通配符模式
    - 多个输入路径的列表
    """
    input_files = []

    # 如果输入是单个字符串，转换为列表
    if isinstance(input_paths, str):
        input_paths = [input_paths]

    for input_path in input_paths:
        if os.path.isfile(input_path):
            # 单个文件
            input_files.append(input_path)
        elif os.path.isdir(input_path):
            # 目录，查找所有 文件
            input_files.extend(glob.glob(os.path.join(input_path, "*")))
        else:
            # 可能是通配符模式
            input_files.extend(glob.glob(input_path))

    # 去重并排序
    return sorted(list(set(input_files)))


def main():
    parser = argparse.ArgumentParser(
        description="将最小顶点覆盖问题编码为 MaxSAT 问题（支持并行处理）"
    )
    parser.add_argument(
        "input",
        nargs="*",
        type=str,
        help="输入的 DIMACS 图文件路径、目录或通配符模式（支持多个输入）",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=str,
        default=None,
        help="输出的 MaxSAT WCNF 文件目录（默认为输入文件所在目录）",
    )
    parser.add_argument(
        "--max-workers",
        type=int,
        default=None,
        help="并行工作线程数（默认为 CPU 核心数）",
    )

    args = parser.parse_args()

    # 检查是否有输入
    if not args.input:
        print("错误：请提供至少一个输入文件、目录或通配符模式")
        parser.print_help()
        sys.exit(1)

    # 收集输入文件
    input_files = collect_input_files(args.input)

    if not input_files:
        print(f"错误：未找到输入文件: {args.input}")
        sys.exit(1)

    print(f"找到 {len(input_files)} 个输入文件")

    with ThreadPoolExecutor(max_workers=args.max_workers) as executor:
        futures = {
            executor.submit(
                process_single_file, input_file, args.output_dir
            ): input_file
            for input_file in input_files
        }
        with tqdm(total=len(futures), desc="处理文件", unit="file") as pbar:
            for future in as_completed(futures):
                try:
                    success, error = future.result()
                    if success:
                        pbar.update(1)
                    else:
                        pbar.update(1)
                        print(f"错误: {error}")
                except Exception as e:
                    pbar.update(1)
                    print(f"错误: {e}")


if __name__ == "__main__":
    main()
