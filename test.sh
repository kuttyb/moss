#!/bin/sh

make -j"$(nproc)"
SWEEP=all JOBS="$(nproc)" bash -x tools/sumt/run.sh ./moss ../sumt-out
#SWEEP=all JOBS="$(nproc)" tools/sumt/run.sh ./moss ../sumt-out 2>&1 | tee ../sumt-out.log
