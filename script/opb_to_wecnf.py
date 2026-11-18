import argparse


def parse_opb(filepath):
    constraints = []
    nvars = 0
    has_objective = False
    objective_var = None

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("*"):
                parts = line.split()
                for i, p in enumerate(parts):
                    if p == "#variable=":
                        nvars = int(parts[i + 1])
                continue
            if line.startswith("min:"):
                has_objective = True
                parts = line.split()
                objective_var = int(parts[2][1:])
                continue

            tokens = line.split()
            coeffs = []
            literals = []
            i = 0
            while i < len(tokens) and tokens[i] != ">=":
                if tokens[i] in ("+1", "-1"):
                    var = int(tokens[i + 1][1:])
                    coeffs.append(int(tokens[i]))
                    if tokens[i] == "-1":
                        literals.append(-var)
                    else:
                        literals.append(var)
                    i += 2
                else:
                    i += 1

            bound_idx = tokens.index(">=")
            bound = int(tokens[bound_idx + 1])
            constraints.append((coeffs, literals, bound))

    return nvars, constraints, has_objective, objective_var


def opb_to_wecnf(input_file, output_file):
    nvars, constraints, has_objective, objective_var = parse_opb(input_file)

    max_var = nvars
    if has_objective and objective_var:
        max_var = max(max_var, objective_var)

    top = 1
    for _, literals, bound in constraints:
        neg_count = sum(1 for l in literals if l < 0)
        knf_bound = bound + neg_count
        top = max(top, knf_bound)
        for l in literals:
            max_var = max(max_var, abs(l))

    with open(output_file, "w") as f:
        f.write(f"p wecnf {max_var} {len(constraints)} {top}\n")
        for coeffs, literals, bound in constraints:
            neg_count = sum(1 for l in literals if l < 0)
            knf_bound = bound + neg_count
            f.write(f"{knf_bound}")
            for coeff, lit in zip(coeffs, literals):
                f.write(f" {coeff} {abs(lit)}")
            f.write(" 0\n")


def main():
    parser = argparse.ArgumentParser(description="Convert OPB to WECNF format")
    parser.add_argument("-f", "--file", required=True, help="Input OPB file")
    parser.add_argument("-o", "--output", required=True, help="Output WECNF file")
    args = parser.parse_args()

    opb_to_wecnf(args.file, args.output)


if __name__ == "__main__":
    main()
