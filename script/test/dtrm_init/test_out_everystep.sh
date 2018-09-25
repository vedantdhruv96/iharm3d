#!/bin/bash

source ../test_common.sh

PROB=${1:-torus}
MADS=${2:-0}

# Must be just a name for now
OUT_DIR=results_everystep_$PROB

# Initial clean and make of work area
BASEDIR=../../..
rm -rf build_archive param.dat harm
make -f $BASEDIR/makefile -j4 PROB=$PROB

rm -rf $OUT_DIR
mkdir -p $OUT_DIR

# Give the system a reasonable size to limit runtime
# Bondi problem is 2D
if [ "$PROB" == "bondi" ]; then
  set_problem_size 256 256 1
else
  set_problem_size 96 48 48
fi

# Give a relatively short endpoint
# We're testing init and basic propagation
set_run_dbl tf 1.0
set_run_dbl DTd 0.0 # Output every step
if [ $PROB == "torus" ]
then
  set_run_dbl u_jitter 0.0
fi

for i in $MADS
do

  rm -rf $OUT_DIR/dumps $OUT_DIR/restarts $OUT_DIR/*.h5

  set_cpu_topo 1 1 1

  make -f $BASEDIR/makefile -j4 PROB=$PROB debug

  if [ $PROB == "torus" ]
  then 
    set_run_int mad_type $i
    echo "First run of torus problem, mad_type $i..."
  else
    echo "First run of $PROB problem..."
  fi
  ./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_firsttime.txt
  echo "Done!"

  cd $OUT_DIR
  mv dumps dumps_gold
  rm -rf restarts
  cd ..

  sleep 1

  make -f $BASEDIR/makefile -j4 PROB=$PROB debug
  echo "Second run..."
  ./harm -p param.dat -o $OUT_DIR > $OUT_DIR/out_secondtime.txt
  echo "Done!"

done