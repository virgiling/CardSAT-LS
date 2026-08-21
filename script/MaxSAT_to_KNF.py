import argparse
import csv
import subprocess
from concurrent.futures import ProcessPoolExecutor, as_completed


def get_formula_data(file):
    data = {}
    candidates = []
    with open(file, mode="r", encoding="utf-8-sig") as csvFile:
        csvReader = csv.DictReader(csvFile)
        for line in csvReader:
            temp_b = line["Name"]
            data[temp_b] = line
            candidates.append(temp_b)
    return candidates, data


def convert_single_formula(task):
    b, bound, wcnf_dir, output_dir = task
    converter = "./tools/maxSAT_to_KNF/maxSAT2KNF"
    in_file = f"{wcnf_dir}/{b}.wcnf"
    ofile_sat = f"{output_dir}/{b}-sat.knf"

    try:
        subprocess.run(
            f"{converter} {in_file} -MaxSAT2KNF {ofile_sat} -add_bound {bound}",
            shell=True,
            check=True,
        )
        return (b, True, None)
    except subprocess.CalledProcessError as e:
        return (b, False, str(e))


def convertmax2knf(wcnf_dir, csv_path, output_dir, max_workers=20):
    candidates, formula_data = get_formula_data(csv_path)

    tasks = []
    for b in candidates:
        bound = int(formula_data[b]["UnsatBound"]) - 1
        if (int(formula_data[b]["SoftUnits"]) - bound) < 2 or bound < 2:
            continue

        bound = (
            int(formula_data[b]["SoftUnits"]) - int(formula_data[b]["UnsatBound"])
        ) + 1
        tasks.append((b, bound, wcnf_dir, output_dir))

    completed = 0
    failed = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {
            executor.submit(convert_single_formula, task): task for task in tasks
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
    parser = argparse.ArgumentParser(description="Convert MaxSAT WCNF to KNF format")
    parser.add_argument(
        "-w", "--wcnf-dir", required=True, help="Directory containing .wcnf input files"
    )
    parser.add_argument(
        "-c", "--csv", required=True, help="CSV file with formula bounds"
    )
    parser.add_argument(
        "-o", "--output-dir", required=True, help="Directory for output .knf files"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=20,
        help="Number of parallel workers (default: 20)",
    )
    args = parser.parse_args()

    convertmax2knf(args.wcnf_dir, args.csv, args.output_dir, args.jobs)


if __name__ == "__main__":
    main()
