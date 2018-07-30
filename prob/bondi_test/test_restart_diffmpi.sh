#!/bin/bash

# Must be just a name for now
OUT_DIR=test_restart

rm -rf $OUT_DIR
mkdir -p $OUT_DIR

# Cleanup any previous invocation or convergence test
sed -i -e "s/N1CPU [0-9]\+/N1CPU 1/g" parameters.h
sed -i -e "s/N2CPU [0-9]\+/N2CPU 1/g" parameters.h
sed -i -e "s/N3CPU [0-9]\+/N3CPU 1/g" parameters.h

# Give the system a reasonable size to limit runtime
sed -i -e "s/N1TOT [0-9]\+/N1TOT 64/g" parameters.h
sed -i -e "s/N2TOT [0-9]\+/N2TOT 64/g" parameters.h

make -f ../../makefile -j4 PROB=$(basename "$PWD")
./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_firsttime.txt

cd $OUT_DIR

cp dumps/dump_00000010.h5 ./last_dump_gold.h5
cp restarts/restart_00000001.h5 .

sleep 1
rm -rf restarts dumps
mkdir restarts
cd restarts
mv ../restart_00000001.h5 .
ln -s restart_00000001.h5 restart.last

cd ../..

sed -i -e "s/N1CPU 1/N1CPU 2/g" parameters.h
sed -i -e "s/N2CPU 1/N2CPU 4/g" parameters.h

make -f ../../makefile -j4 PROB=$(basename "$PWD")
OMP_NUM_THREADS=2 mpirun -n 8 ./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_secondtime.txt

cd $OUT_DIR

# Verification
set -x

grep restart out_firsttime.txt

grep restart out_secondtime.txt

h5diff --delta=1e-12 last_dump_gold.h5 dumps/dump_00000010.h5

# Jcon is pretty random. Show it's not really different
h5diff --delta=1e-3 last_dump_gold.h5 dumps/dump_00000010.h5