# CardSAT-LS

## Build

Requirements: CMake 3.15 or newer, GNU Make, and a C++20 compiler.

Build the optimized solver:

```bash
make release
```

The executable is `build/cardsat`. `make install` copies it to `bin/cardsat`.

The release build uses host-specific instructions and interprocedural
optimization. For a portable binary:

```bash
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release \
  -DCARDSAT_NATIVE_OPTIMIZATION=OFF
cmake --build build-portable --parallel
```

Build the conversion tools:

```bash
make tools
```

This creates `tools/maxSAT_to_KNF/maxSAT2KNF` and
`tools/wecnf_to_wcnf/wecnf_to_wcnf`.

## Run

```bash
./build/cardsat path/to/instance.knf
./build/cardsat --mode 1 path/to/instance.knf
./build/cardsat --help
```

Mode 0 is the default local search. It can find a satisfying assignment but
cannot prove UNSAT. Mode 1 uses the vendored CCDCL backend.

The solver prints one of these status lines:

```text
s SATISFIABLE
s UNSATISFIABLE
s UNKNOWN
```

A SAT result is followed by a `v` line with the model. Normal completion
returns exit code 0 for all three statuses. Parse the `s` line. Do not use the
exit code as the SAT result.

The checked-in experiment results use `--mode 0 --preprocessing 1 --init_mode 1
--enable_swap 1 --seeds 20` and a 3600 second cutoff. The current binary
defaults may differ. Pass these options explicitly when reproducing the
results.

### KNF input

```text
p knf <variables> <rows>
k <degree> <lit1> <lit2> ... <litN> 0
<lit1> <lit2> ... <litN> 0
```

A row that starts with `k` requires at least `degree` of its literals to be
true. A CNF clause has degree 1. Example:

```text
p knf 3 2
1 -2 0
k 2 1 2 3 0
```

## Reproduce experiments

Install the Python dependencies:

```bash
uv sync --frozen
```

### Prepare the benchmarks

The benchmark archives are not included. Obtain the input files first.

MaxSAT24 starts from unweighted partial MaxSAT WCNF:

```bash
make tools
mkdir -p /path/to/knf
uv run python script/MaxSAT_to_KNF.py \
  --wcnf-dir /path/to/wcnf \
  --csv data/maxsat-2024-bounds.csv \
  --output-dir /path/to/knf \
  --jobs 10
```

MVC starts from DIMACS graphs:

```bash
uv run python script/MVC_to_MaxSAT.py \
  '/path/to/graphs/*.dimacs' \
  --output-dir /path/to/wcnf \
  --max-workers 10
uv run python script/MaxSAT_to_KNF.py \
  --wcnf-dir /path/to/wcnf \
  --csv data/mvc-bounds.csv \
  --output-dir /path/to/knf \
  --jobs 10
```

WSNO starts from WECNF:

```bash
uv run python script/wecnf_to_wcnf.py \
  --wecnf-dir /path/to/wecnf \
  --output-dir /path/to/wcnf \
  --jobs 10
uv run python script/MaxSAT_to_KNF.py \
  --wcnf-dir /path/to/wcnf \
  --csv data/wsno-bounds.csv \
  --output-dir /path/to/knf \
  --jobs 10
```

`wecnf_to_wcnf.py` maps rows with weight at or above `top` to hard clauses
and sets every other soft weight to 1. It is not semantics-preserving for
arbitrary weighted WECNF inputs.

DES starts from directories that contain `mobsfile`, `faultfile`, and
`ccfile.N`:

```bash
uv run python script/DES_to_KNF.py \
  --base_dir /path/to/DES \
  --output_dir /path/to/knf \
  --threads 10 \
  --generate_cnf
```

SAT25 uses an external CCDCL cardinality-extraction step. This repository does
not include that script.

`MaxSAT_to_KNF.py` reads a CSV with the columns `Name,UnsatBound,SoftUnits`.
For each row it calls `maxSAT2KNF` with `B = SoftUnits - UnsatBound + 1`, so
the cardinality row has degree `UnsatBound - 1`. Rows where either side of the
bound is smaller than 2 are skipped. The converter treats all non-hard soft
clauses as unit weight.

For WSNO, the CNF local-search baselines run on the original WSNO CNF. Their
CSV rows use the KNF `<name>-sat` names for alignment. Unknown rows store
`Time=7200`, which is 2 x 3600 seconds.

### Run a benchmark

```bash
JOBS=10 TIMEOUT_SECONDS=3600 MEMORY_LIMIT_KIB=31457280 \
  bash script/run_experiments.sh \
  ./build/cardsat /path/to/benchmark /path/to/results \
  --mode 0
```

The runner reads `<benchmark-dir>/satvbs.txt` by default. Set `INSTANCE_LIST`
to use another list. The environment variables are:

- `JOBS`: number of parallel solver processes.
- `TIMEOUT_SECONDS`: wall-clock limit per instance.
- `MEMORY_LIMIT_KIB`: virtual-memory limit per process. `0` disables it.
- `TIMEOUT_COMMAND`: timeout command. Use `gtimeout` on macOS.
- `INSTANCE_LIST`: instance list path.

The runner writes one log per instance. The log name is the SHA-256 of the
list entry. Each log contains the instance path, the solver output, the
`time -p` output, and the runner exit status. The runner does not validate
models. Rerunning an entry overwrites its log.

The runner requires Bash, `sha256sum`, `/usr/bin/time`, FIFO support, and
`ulimit -v` support when a memory limit is set.

### Convert logs to statistics CSV

```bash
uv run python script/stats_to_csv.py \
  --result-dir /path/to/results \
  --instance-list /path/to/benchmark/satvbs.txt \
  --output data/csv/MaxSAT24/cardsat-new_stats.csv \
  --cutoff 3600 \
  --summary-json output/cardsat-new-summary.json
```

Pass the same instance list used by the runner. The script reads the solver's
`s` line, the final `real` time, and the runner exit status. It writes
`Instance,Result,Time,Best,Mono`. Missing, malformed, conflicting, or
cutoff-reaching results become `unknown`. Unknown results use PAR-2, which is
`2 x cutoff` by default. `Best` and `Mono` are written as `No`; the table and
figure scripts do not use them.

The command prints `#All`, `#SAT`, `#UNSAT`, `#Solved`, `#Unsolved`, PAR-2,
and diagnostic counts. `--summary-json` is optional.

For the 896-instance integration corpus, use `data/csv/SAT25/satvbs-all.txt`
for SAT25. The ordinary `satvbs.txt` files select the 814-instance main corpus.

### Make the table and figures

The scripts read `data/csv/<benchmark>/*_stats.csv`. Run them from the
repository root:

```bash
mkdir -p output
uv run python script/draw/table_1.py > output/table_1.tex
uv run python script/draw/figure_1.py
uv run python script/draw/figure_2.py
uv run python script/draw/figure_3.py
```

Outputs:

- `table_1.py`: table on standard output.
- `figure_1.py`: `output/All-Bench-All-Solver.pdf`.
- `figure_2.py`: `output/CardSAT-LS-Total.pdf`.
- `figure_3.py`: `output/CCDCL-LS_vs_CCDCL.pdf`.

For complete solvers, `#Solved` is `#SAT + #UNSAT`.

## Citation

Paper: [IJCAI proceedings](https://www.ijcai.org/proceedings/2026/251).
Local copy: [data/CardSAT-LS.pdf](data/CardSAT-LS.pdf).
DOI: [10.24963/ijcai.2026/251](https://doi.org/10.24963/ijcai.2026/251).

Shuli Hu, Dian Ling, Jiaqi Li, and Minghao Yin. "Towards Cardinality-Aware
Local Search for SAT with Cardinality Constraints." In Proceedings of the
Thirty-Fifth International Joint Conference on Artificial Intelligence
(IJCAI-26), pages 2256-2264, 2026.

BibLaTeX entry: [CITATION.bib](CITATION.bib).
