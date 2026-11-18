import argparse
import os
import subprocess
from concurrent.futures import ProcessPoolExecutor, as_completed


def convert_single_formula(task):
    b, wecnf_dir, output_dir = task
    converter = "./tools/wecnf_to_wcnf/wecnf_to_wcnf"
    in_file = f"{wecnf_dir}/{b}.wecnf"
    ofile_wcnf = f"{output_dir}/{b}.wcnf"

    try:
        subprocess.run(
            f"{converter} {in_file} {ofile_wcnf}",
            shell=True,
            check=True,
        )
        return (b, True, None)
    except subprocess.CalledProcessError as e:
        return (b, False, str(e))


def convert_wecnf_to_wcnf(wecnf_dir, output_dir, max_workers=20):
    tasks = []
    for b in os.listdir(wecnf_dir):
        tasks.append(b.replace(".wecnf", ""))

    completed = 0
    failed = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {
            executor.submit(convert_single_formula, (task, wecnf_dir, output_dir)): task
            for task in tasks
        }
        for future in as_completed(futures):
            name, success, error = future.result()
            completed += 1
            if success:
                print(f"[{completed}/{len(tasks)}] Completed: {name}")
            else:
                print(f"[{completed}/{len(tasks)}] Failed: {name} - {error}")
                failed.append(name)

    if failed:
        print(f"Failed instances: {failed}")


def main():
    parser = argparse.ArgumentParser(description="Convert WECNF to WCNF format")
    parser.add_argument("-w", "--wecnf-dir", required=True, help="Directory containing .wecnf input files")
    parser.add_argument("-o", "--output-dir", required=True, help="Directory for output .wcnf files")
    parser.add_argument("-j", "--jobs", type=int, default=20, help="Number of parallel workers (default: 20)")
    args = parser.parse_args()

    convert_wecnf_to_wcnf(args.wecnf_dir, args.output_dir, args.jobs)


if __name__ == "__main__":
    main()
