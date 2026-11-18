#!/usr/bin/env python3
"""
将WCNF（加权CNF）文件转换为标准CNF文件
使用pysat库进行转换
"""

import sys
import argparse
from pysat.formula import WCNF, CNF
from pysat.card import *


def wcnf_to_cnf(input_file, bound: int = None, output_file=None):
    """
    将WCNF文件转换为CNF文件
    
    Args:
        input_file: 输入的WCNF文件路径
        output_file: 输出的CNF文件路径，如果为None则自动生成
    """
    # 读取WCNF文件
    wcnf = WCNF(from_file=input_file)
    
    # 创建CNF对象
    cnf = CNF()
    
    # 添加硬子句
    for clause in wcnf.hard:
        cnf.append(clause)
    
    # 添加软子句（将软子句也视为硬子句）
    if bound is not None:
        literals = [lit[0] for lit in wcnf.soft]
        max_var = len(literals)
        new_cnf = CardEnc.atmost(literals, max_var - bound, max_var, encoding=EncType.kmtotalizer)
        for clause in new_cnf:
            cnf.append(clause)
    
    # 确定输出文件名
    if output_file is None:
        if input_file.endswith('.wcnf'):
            output_file = input_file[:-5] + '.cnf'
        else:
            output_file = input_file + '.cnf'
    
    # 保存CNF文件
    cnf.to_file(output_file)
    
    print(f"转换完成: {input_file} -> {output_file}")
    print(f"变量数: {cnf.nv}, 子句数: {len(cnf.clauses)}")
    
    return output_file


def main():
    parser = argparse.ArgumentParser(
        description='将WCNF（加权CNF）文件转换为标准CNF文件',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        'input',
        type=str,
        help='输入的WCNF文件路径'
    )
    parser.add_argument(
        '-o', '--output',
        type=str,
        default=None,
        help='输出的CNF文件路径（默认为输入文件名替换扩展名）'
    )
    parser.add_argument(
        '-b', '--bound',
        type=int,
        default=None,
        help='bound值（默认为None，表示不使用bound）'
    )
    
    args = parser.parse_args()
    
    try:
        wcnf_to_cnf(args.input, args.bound, args.output)
    except FileNotFoundError:
        print(f"错误: 找不到文件 {args.input}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"错误: {str(e), traceback.format_exc()}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()