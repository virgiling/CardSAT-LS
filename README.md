# CardSAT-LS

CardSAT-LS is a cardinality-aware stochastic local-search solver for formulas
in KNF, where ordinary CNF clauses and cardinality constraints are represented
natively. It combines model-recoverable preprocessing, fake-backbone
initialization, a make-break/violation score, and adaptive flip/swap operators.

The solver has two execution modes:

- `--mode 0` (default) runs the serial, incomplete CardSAT-LS search. It can
  find satisfying assignments but cannot in general certify UNSAT.
- `--mode 1` selects the vendored CCDCL backend.

The submitted manuscript accepted at IJCAI 2026, *Towards Cardinality-Aware
Local Search for SAT with Cardinality Constraints*, is available at
[`data/CardSAT-LS.pdf`](data/CardSAT-LS.pdf). This file is the
submitted/accepted manuscript, not a subsequently corrected version.

## Build

Requirements:

- CMake 3.15 or newer
- GNU Make
- a C++20 compiler

From the repository root, build the optimized solver with:

```bash
make release
```

The executable is `build/cardsat`. `make install` also copies it to
`bin/cardsat`. The release build enables host-specific instructions and
interprocedural optimization. The vendored CCDCL dependency is staged in the
selected build directory, so the build does not modify `lib/ccdcl/`.

To build a binary without host-specific instructions:

```bash
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release \
  -DCARDSAT_NATIVE_OPTIMIZATION=OFF
cmake --build build-portable --parallel
```

Optional conversion utilities can be compiled with:

```bash
make tools
```

This builds `tools/maxSAT_to_KNF/maxSAT2KNF` and
`tools/wecnf_to_wcnf/wecnf_to_wcnf`; they are not built by `make release`.

## Run

```bash
./build/cardsat path/to/instance.knf
./build/cardsat --mode 1 path/to/instance.knf
./build/cardsat --help
```

### Default CardSAT-LS profile

With no parameter overrides, `--mode 0` uses the audited single-configuration
profile selected after the restart/swap repair. Its fixed experiment controls
are `mode=0`, `preprocessing=1`, `init_mode=1`, `enable_swap=1`, and `seeds=20`;
the recorded experiment cutoff is 3,600 seconds. These controls were constants,
not dimensions of the local-search tuning space. Their command-line switches
remain available to select the hybrid backend or reproduce controlled
ablations and diagnostics.

The solver reports one of these status lines:

```text
s SATISFIABLE
s UNSATISFIABLE
s UNKNOWN
```

A SAT result is followed by a `v` line containing the recovered model. On
normal completion, all three statuses return process exit code `0`; automation
must parse the `s` line instead of using the exit code as the SAT result.

### KNF input

```text
p knf <variables> <rows>
k <degree> <lit1> <lit2> ... <litN> 0
<lit1> <lit2> ... <litN> 0
```

A row beginning with `k` requires at least `degree` of its literals to be true.
An ordinary CNF clause has implicit degree 1. For example:

```text
p knf 3 2
1 -2 0
k 2 1 2 3 0
```

## Experiment and conversion utilities

Run the commands in this section from the repository root. Install the Python
dependencies with:

```bash
uv sync --frozen
```

Except where noted below, the files in `script/` and the WECNF converter are
CardSAT-LS paper-artifact utilities and carry no separate upstream attribution.
They target the benchmark formats used in the experiments rather than every
variant of the similarly named public formats. `tools/maxSAT_to_KNF/` was
imported from
[`jreeves3/LiteralSorting`](https://github.com/jreeves3/LiteralSorting/tree/a3ce119328d15beab1597f77f59e094e129c1e);
its parser records that it was adapted from
[`jreeves3/unsat-proof-skeletons`](https://github.com/jreeves3/unsat-proof-skeletons),
originally by Benjamin Kiesl-Ritter. The retained notices specify MIT and
MIT-0 licensing, respectively.

### Preparing the paper benchmarks

The paper used the utilities in these benchmark-specific stages:

| Benchmark | Starting format | Preparation stage in this repository |
| --- | --- | --- |
| MaxSAT24 | unweighted partial MaxSAT WCNF | `MaxSAT_to_KNF.py` with `data/maxsat-2024-bounds.csv` |
| DES | directories containing `mobsfile`, `faultfile`, and `ccfile.N` | `DES_to_KNF.py` |
| MVC | DIMACS graph | `MVC_to_MaxSAT.py`, then `MaxSAT_to_KNF.py` with `data/mvc-bounds.csv` |
| WSNO | WECNF | `wecnf_to_wcnf.py`, then `MaxSAT_to_KNF.py` with `data/wsno-bounds.csv` |
| SAT25 | CNF | external CCDCL cardinality extraction; no preparation script is included here |

The source benchmark archives are not included, so these stages assume that
the corresponding input files have already been obtained.

#### DES to KNF

`script/DES_to_KNF.py` traverses the DES benchmark layout, finds directories
containing both `mobsfile` and `faultfile`, and combines those files with the
largest numbered `ccfile.N` bound to produce KNF instances. With
`--generate_cnf`, it also produces the corresponding CNF files used by the
artifact workflow.

```bash
uv run python script/DES_to_KNF.py \
  --base_dir /path/to/DES \
  --output_dir /path/to/knf \
  --threads 10 \
  --generate_cnf
```

This is a legacy converter for the original DES directory and file layout,
not a general DIMACS-to-KNF converter. Check generated headers and instances
before applying it to a different dataset.

#### Minimum vertex cover to MaxSAT

`script/MVC_to_MaxSAT.py` encodes a graph in DIMACS `p edge`/`e u v` form as
unweighted partial MaxSAT: each edge becomes a hard covering clause, and each
vertex contributes a unit soft clause. Inputs may be files, directories, or
quoted glob patterns.

```bash
uv run python script/MVC_to_MaxSAT.py \
  '/path/to/graphs/*.dimacs' \
  --output-dir /path/to/wcnf \
  --max-workers 10
```

When `--output-dir` is omitted, each `.wcnf` file is written beside its input.
Directory inputs should contain only graph instances; equal filename stems
map to the same output name.

#### Unweighted MaxSAT to KNF

`script/MaxSAT_to_KNF.py` is the batch wrapper used for the MaxSAT24, MVC, and
WSNO paper instances. It reads a bounds CSV with the exact columns
`Name,UnsatBound,SoftUnits`, loads `<Name>.wcnf`, derives the SAT threshold,
and writes `<Name>-sat.knf`.

```bash
make tools
mkdir -p /path/to/knf
uv run python script/MaxSAT_to_KNF.py \
  --wcnf-dir /path/to/wcnf \
  --csv data/maxsat-2024-bounds.csv \
  --output-dir /path/to/knf \
  --jobs 10
```

The other supplied bound files are `data/mvc-bounds.csv` and
`data/wsno-bounds.csv`. The underlying converter can also be invoked directly:

```bash
./tools/maxSAT_to_KNF/maxSAT2KNF INPUT.wcnf \
  -MaxSAT2KNF OUTPUT.knf -add_bound B
```

Here `B` is the allowed number of falsified soft clauses. The converter copies
hard clauses, introduces an auxiliary literal for each non-unit soft clause,
and adds one cardinality row over the soft-clause literals. It is intended for
unweighted partial MaxSAT (or uniform soft weights): different non-hard
weights are treated equally, and native WKNF cardinality rows are not carried
through.

For a CSV row with `S = SoftUnits` and `U = UnsatBound`, the wrapper calls the
converter with `B = S - U + 1`, so the generated cardinality row has degree
`S - B = U - 1`. Rows for which either side of this bound is smaller than 2
are skipped by the wrapper.

The SAT25 instances followed a separate CCDCL cardinality-extraction workflow;
this repository does not contain a SAT25 conversion script.

### Format helpers

These scripts helped inspect or prepare intermediate artifact formats. Their
accepted subsets are deliberately narrow.

- `script/maxsat_to_CNF.py` copies the hard clauses of a WCNF file and, when
  `--bound B` is supplied, uses PySAT's totalizer over the first literal of
  each soft clause. It is intended only for the unit-soft benchmark convention.
  Without `--bound`, soft clauses are omitted.

  ```bash
  uv run python script/maxsat_to_CNF.py INPUT.wcnf \
    --output OUTPUT.cnf --bound B
  ```

- `script/knf_to_opb.py` prints the CNF and cardinality rows of a KNF file as
  unit-coefficient `>=` OPB constraints. `--opbo` additionally emits a dummy
  minimization objective.

  ```bash
  uv run python script/knf_to_opb.py --file INPUT.knf > OUTPUT.opb
  uv run python script/knf_to_opb.py --file INPUT.knf --opbo > OUTPUT.opb
  ```

- `script/opb_to_wecnf.py` reads the restricted OPB form produced by the
  preceding helper and writes the project's WECNF representation. It supports
  only `+1`/`-1` coefficients and `>=` constraints; an objective is not
  preserved.

  ```bash
  uv run python script/opb_to_wecnf.py \
    --file INPUT.opb --output OUTPUT.wecnf
  ```

- `script/wecnf_to_wcnf.py` batch-invokes the repository's C++ WECNF helper.
  It maps rows at or above `top` to hard WCNF clauses and normalizes every
  other row weight to 1.

  ```bash
  make tools
  mkdir -p /path/to/wcnf
  uv run python script/wecnf_to_wcnf.py \
    --wecnf-dir /path/to/wecnf \
    --output-dir /path/to/wcnf \
    --jobs 10
  ```

  A single file can instead be converted directly:

  ```bash
  ./tools/wecnf_to_wcnf/wecnf_to_wcnf INPUT.wecnf OUTPUT.wcnf
  ```

  The input directory should contain only `.wecnf` files. Because soft weights
  are normalized, this is not a semantics-preserving converter for arbitrary
  weighted instances.

The KNF, OPB, WECNF, and WCNF helpers are not a general round-trip conversion
pipeline. In particular, the WECNF helper's coefficient-pair representation
and the WCNF helper's first-field interpretation serve different artifact
stages.

### Running experiments

`script/run_experiments.sh` is a Linux-oriented, instance-list-driven runner.
It launches independent solver processes in parallel; each mode-0 CardSAT-LS
process remains serial. The default instance list is
`<benchmark-dir>/satvbs.txt`. Blank lines and lines beginning with `#` are
ignored, and relative instance paths are resolved from the benchmark directory.

```bash
JOBS=10 TIMEOUT_SECONDS=3600 MEMORY_LIMIT_KIB=67108864 \
  bash script/run_experiments.sh \
  ./build/cardsat /path/to/benchmark /path/to/results \
  --mode 0
```

The example applies a 3,600-second wall timeout and a 64 GiB per-process
virtual-memory limit. `INSTANCE_LIST=/path/to/list.txt` selects another list;
`TIMEOUT_COMMAND=gtimeout` may select GNU coreutils' timeout command where
available. The runner also requires Bash, `sha256sum`, `/usr/bin/time`, FIFO
support, and a platform supporting `ulimit -v` when a memory limit is set.

Each result is stored as `<sha256-of-list-entry>.log`. The log records the
original instance path as `runner_instance`, followed by solver output,
portable `time -p` measurements, and `runner_exit_status`. The exit status is
not the logical SAT result; parse the solver's `s` line. This runner does not
validate returned models, and rerunning the same list entry overwrites its log.

### Converting experiment logs to statistics CSV

`script/stats_to_csv.py` converts the logs produced by
`script/run_experiments.sh` into the `Instance,Result,Time,Best,Mono` schema
consumed by the table and figure scripts. Pass the exact same instance list
used by the runner because each log filename is the SHA-256 digest of its
original list entry.

```bash
uv run python script/stats_to_csv.py \
  --result-dir /path/to/results \
  --instance-list /path/to/benchmark/satvbs.txt \
  --output data/csv/MaxSAT24/cardsat-new_stats.csv \
  --cutoff 3600 \
  --summary-json output/cardsat-new-summary.json
```

The script reads the solver's `s` line, the final `/usr/bin/time -p` `real`
measurement, and `runner_exit_status`. It accepts normal CardSAT-LS exit code
0 and conventional SAT-solver exit codes 10 for SAT and 20 for UNSAT; `OPTIMUM
FOUND` with exit code 30 is recorded as SAT. Missing, malformed, conflicting,
or cutoff-reaching results are written as `unknown`. Their PAR score is
`cutoff * --par-factor`, where the default factor is 2. `Best` and `Mono` are
written as `No` because they require a separate cross-solver comparison and do
not affect the repository's solved-count or PAR-2 calculations.

After writing the CSV, the command prints `#All`, `#SAT`, `#UNSAT`, `#Solved`,
`#Unsolved`, PAR-2, and diagnostic counts. `--summary-json` is optional. For
the 896-instance complete-solver corpus, use
`data/csv/SAT25/satvbs-all.txt` for SAT25; the ordinary `satvbs.txt` files
select the 814-instance SAT comparison corpus. This conversion step parses
reported statuses but does not validate SAT models or UNSAT proofs.

## Paper tables and figures

The analysis CSVs are under `data/csv/<benchmark>/`. The standard CardSAT-LS
plotting alias contains the post-hoc cutoff-sensitivity selection: 557/814
solved with PAR-2 2369.06. It combines the measured repair-retune run with five
strict-valid near-cutoff outcomes from two prior runs and must not be described
as one serial execution. The measured single-configuration 552/814 result is
archived under `output/cardsat-ls-repair-retune552/`. See
[`data/csv/README.md`](data/csv/README.md) for provenance, limitations, and
metric definitions.

The plotting programs are fixed-path paper scripts rather than generic CLIs:

| Script | Purpose and data scope | Output |
| --- | --- | --- |
| `script/draw/table_1.py` | Table 1 over the five benchmark families and 814-instance main corpus | standard output |
| `script/draw/figure_1.py` | all-solver comparison over the 814-instance main corpus | `output/All-Bench-All-Solver.pdf` |
| `script/draw/figure_2.py` | five CardSAT-LS ablation curves over the 814-instance main corpus | `output/CardSAT-LS-Total.pdf` |
| `script/draw/figure_3.py` | CCDCL-LS versus CCDCL over the 896-instance integration corpus | `output/CCDCL-LS_vs_CCDCL.pdf` |

The 814-instance corpus is selected by the five
`data/csv/<benchmark>/satvbs.txt` files. The 896-instance integration corpus
uses the expanded `data/csv/SAT25/satvbs-all.txt` list for SAT25.

For complete solvers, report `#Solved/#All` with
`#Solved = #SAT + #UNSAT`. 

Recreate the table and figures from the checked-in CSVs with:

```bash
mkdir -p output
uv run python script/draw/table_1.py > output/table_1.tex
uv run python script/draw/figure_1.py
uv run python script/draw/figure_2.py
uv run python script/draw/figure_3.py
```

## Citation

> Shuli Hu, Dian Ling, Jiaqi Li, and Minghao Yin. Towards Cardinality-Aware
> Local Search for SAT with Cardinality Constraints. In Proceedings of the
> Thirty-Fifth International Joint Conference on Artificial Intelligence
> (IJCAI), 2026.

## License and third-party software

CardSAT-LS is released under the [MIT License](LICENSE). The vendored CCDCL
backend carries its own [license](lib/ccdcl/LICENSE). The imported MaxSAT
converter retains its upstream attribution and licensing information in its
source headers, and `include/vec.hpp` retains the MiniSat copyright and license
notice from which that utility was derived.
