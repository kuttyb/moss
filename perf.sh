#! /bin/sh

cd ~/daji/code/moss3 && git checkout main && make moss-opt     # tot
for t in phase25-delivered-v9 phase26-3-let-alias-m1; do
  git worktree add --detach ../perf-$t $t
  make -C ../perf-$t -j moss-opt
done

R=benchmarks/phase25_4/cpp_baseline/run.py
python3 $R --mode timing --reps 5 --skip-report --moss ../perf-phase25-delivered-v9/build/compiler/moss-opt --results-dir tmp/perf/p25
python3 $R --mode timing --reps 5 --skip-report --moss ../perf-phase26-3-let-alias-m1/build/compiler/moss-opt --results-dir tmp/perf/p26
python3 $R --mode timing --reps 5 --skip-report --moss build/compiler/moss-opt --results-dir tmp/perf/tot

python3 /tmp/perf_compare.py p25=tmp/perf/p25/results.csv p26=tmp/perf/p26/results.csv tot=tmp/perf/tot/results.csv

