#!/usr/bin/env bash
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2016 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#
# Call with "make testclustercpx"
#
# The queue is passed via $QUEUE (possibly defined in a local makefile in scip/make/local).
#
# For each run, we can specify the number of nodes reserved for a run via $PPN. If tests runs
# with valid time measurements should be executed, this number should be chosen in such a way
# that a job is run on a single computer, i.e., in general, $PPN should equal the number of cores
# of each computer. If course, the value depends on the specific computer/queue.
#
# To get the result files call "./evalcheck_cluster_cplex.sh
# results/check.$TSTNAME.$BINNAME.$SETNAME.eval in directory check/
# This leads to result files
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.out
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.res
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.err

TSTNAME=$1
BINNAME=$2
SETNAME=$3
BINID=$BINNAME.$4
TIMELIMIT=$5
NODELIMIT=$6
MEMLIMIT=$7
THREADS=$8
FEASTOL=$9
DISPFREQ=${10}
CONTINUE=${11}
QUEUETYPE=${12}
QUEUE=${13}
PPN=${14}
CLIENTTMPDIR=${15}
NOWAITCLUSTER=${16}
EXCLUSIVE=${17}


# get current SCIP path
SCIPPATH=`pwd`

if test ! -e $SCIPPATH/results
then
    mkdir $SCIPPATH/results
fi

SETTINGS=$SCIPPATH/../settings/$SETNAME.prm

# check if the settings file exists
if test $SETNAME != "default"
then
    if test ! -e $SETTINGS
    then
        echo skipping test due to non-existence of settings file $SETTINGS
        exit
    fi
fi

# check if queue has been defined
if test "$QUEUE" = ""
then
    echo Skipping test since the queue name has not been defined.
    exit
fi

# check if number of nodes has been defined
if test "$PPN" = ""
then
    echo Skipping test since the number of nodes has not been defined.
    exit
fi

# check if the slurm blades should be used exclusively
if test "$EXCLUSIVE" = "true"
then
    EXCLUSIVE=" --exclusive"
else
    EXCLUSIVE=""
fi

# we add 100% to the hard time limit and additional 600 seconds in case of small time limits
# NOTE: the jobs should have a hard running time of more than 5 minutes; if not so, these
#       jobs get automatically assigned in the "express" queue; this queue has only 4 CPUs
#       available
HARDTIMELIMIT=`expr \`expr $TIMELIMIT + 600\` + $TIMELIMIT`

#define clusterqueue, which might not be the QUEUE, cause this might be an alias for a bunch of QUEUEs
CLUSTERQUEUE=$QUEUE

NICE=""
ACCOUNT="mip"

if test $CLUSTERQUEUE = "dbg"
then
    CLUSTERQUEUE="mip-dbg,telecom-dbg"
    ACCOUNT="mip-dbg"
elif test $CLUSTERQUEUE = "telecom-dbg"
then
    ACCOUNT="mip-dbg"
elif test $CLUSTERQUEUE = "mip-dbg"
then
    ACCOUNT="mip-dbg"
elif test $CLUSTERQUEUE = "opt-low"
then
    CLUSTERQUEUE="opt"
    NICE="--nice=10000"
fi

# we add 10% to the hard memory limit and additional 100mb to the hard memory limit
HARDMEMLIMIT=`expr \`expr $MEMLIMIT + 100\` + \`expr $MEMLIMIT / 10\``

# in case of qsub queue the memory is measured in kB and in case of srun the time needs to be formatted
if test  "$QUEUETYPE" = "qsub"
then
    HARDMEMLIMIT=`expr $HARDMEMLIMIT \* 1024000`
else
    MYMINUTES=0
    MYHOURS=0
    MYDAYS=0
    #calculate seconds, minutes, hours and days
    MYSECONDS=`expr $HARDTIMELIMIT % 60`
    TMP=`expr $HARDTIMELIMIT / 60`
    if test "$TMP" != "0"
    then
        MYMINUTES=`expr $TMP % 60`
        TMP=`expr $TMP / 60`
    if test "$TMP" != "0"
    then
        MYHOURS=`expr $TMP % 24`
        MYDAYS=`expr $TMP / 24`
    fi
    fi
    #format seconds to have two characters
    if test ${MYSECONDS} -lt 10
    then
        MYSECONDS=0${MYSECONDS}
    fi
    #format minutes to have two characters
    if test ${MYMINUTES} -lt 10
    then
        MYMINUTES=0${MYMINUTES}
    fi
    #format hours to have two characters
    if test ${MYHOURS} -lt 10
    then
        MYHOURS=0${MYHOURS}
    fi
    #format HARDTIMELIMT
    if test ${MYDAYS} = "0"
    then
        HARDTIMELIMIT=${MYHOURS}:${MYMINUTES}:${MYSECONDS}
    else
        HARDTIMELIMIT=${MYDAYS}-${MYHOURS}:${MYMINUTES}:${MYSECONDS}
    fi
fi

EVALFILE=$SCIPPATH/results/check.$TSTNAME.$BINID.$QUEUE.$SETNAME"_"$THREADS.eval
echo > $EVALFILE

# counter to define file names for a test set uniquely
COUNT=0

for i in `cat testset/$TSTNAME.test` DONE
do
  if test "$i" = "DONE"
  then
      break
  fi

  # increase the index for the inctance tried to solve, even if the filename does not exist
  COUNT=`expr $COUNT + 1`

  # check if problem instance exists
  if test -f $SCIPPATH/$i
  then

      echo adding instance $COUNT to queue

      # the cluster queue has an upper bound of 2000 jobs; if this limit is
      # reached the submitted jobs are dumped; to avoid that we check the total
      # load of the cluster and wait until it is save (total load not more than
      # 1900 jobs) to submit the next job.
      if test "$NOWAITCLUSTER" != "1"
      then
          ./waitcluster.sh 1500 $QUEUE 200
      fi

      SHORTFILENAME=`basename $i .gz`
      SHORTFILENAME=`basename $SHORTFILENAME .mps`
      SHORTFILENAME=`basename $SHORTFILENAME .lp`
      SHORTFILENAME=`basename $SHORTFILENAME .opb`

      FILENAME=$USER.$TSTNAME.$COUNT"_"$SHORTFILENAME.$QUEUE.$BINID.$SETNAME"_"$THREADS
      BASENAME=$SCIPPATH/results/$FILENAME

      TMPFILE=$BASENAME.tmp
      SETFILE=$BASENAME.prm

      echo $BASENAME >> $EVALFILE

      # in case we want to continue we check if the job was already performed
      if test "$CONTINUE" != "false"
      then
          if test -e results/$FILENAME.out
          then
              echo skipping file $i due to existing output file $FILENAME.out
          continue
          fi
      fi

      if test -e $SETFILE
      then
          rm -f $SETFILE
      fi

      echo > $TMPFILE
      echo ""                              > $TMPFILE
      if test $SETNAME != "default"
      then
          echo read $SETTINGS                  >> $TMPFILE
          echo disp settings changed           >> $TMPFILE
      fi
      if test $FEASTOL != "default"
      then
          echo set simplex tolerances feas $FEASTOL    >> $TMPFILE
          echo set mip tolerances integrality $FEASTOL >> $TMPFILE
      fi
      echo set timelimit $TIMELIMIT           >> $TMPFILE
      echo set clocktype 0                    >> $TMPFILE
      echo set mip display 3                  >> $TMPFILE
      echo set mip interval $DISPFREQ         >> $TMPFILE
      echo set mip tolerances mipgap 0.0      >> $TMPFILE
      echo set mip limits nodes $NODELIMIT    >> $TMPFILE
      echo set mip limits treememory $MEMLIMIT >> $TMPFILE
      echo set threads $THREADS               >> $TMPFILE
      echo set parallel 1                     >> $TMPFILE
      echo set lpmethod 4                     >> $TMPFILE
      echo set barrier crossover -1           >> $TMPFILE
      echo write $SETFILE                     >> $TMPFILE
      echo read $SCIPPATH/$i                  >> $TMPFILE
      echo display problem stats              >> $TMPFILE
      echo optimize                           >> $TMPFILE
      echo display solution quality           >> $TMPFILE
      echo quit                               >> $TMPFILE

      # additional environment variables needed by run.sh
      export SOLVERPATH=$SCIPPATH
      export EXECNAME=$BINNAME
      export BASENAME=$FILENAME
      export FILENAME=$i
      export CLIENTTMPDIR=$CLIENTTMPDIR

      # check queue type
      if test  "$QUEUETYPE" = "srun"
      then
         sbatch --job-name=CPLEX$SHORTFILENAME --mem=$HARDMEMLIMIT -p $CLUSTERQUEUE -A $ACCOUNT --time=${HARDTIMELIMIT} ${NICE} ${EXCLUSIVE} --output=/dev/null run.sh
      else
         qsub -l walltime=$HARDTIMELIMIT -l mem=$HARDMEMLIMIT -l nodes=1:ppn=$PPN -N CPLEX$SHORTFILENAME -V -q $QUEUE -o /dev/null -e /dev/null run.sh
      fi
  else
      echo "input file "$SCIPPATH/$i" not found!"
  fi
done

