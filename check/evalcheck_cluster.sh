#!/usr/bin/env bash
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2010 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

export LANG=C

REMOVE=0
AWKARGS=""
FILES=""

for i in $@
do
  if test ! -e $i
  then
      if test "$i" = "-r"
      then
	  REMOVE=1
      else
	  AWKARGS="$AWKARGS $i"
      fi
  else	
      FILES="$FILES $i"
  fi
done

for FILE in $FILES
do
 
  DIR=`dirname $FILE`
  EVALFILE=`basename $FILE .eval`
  EVALFILE=`basename $EVALFILE .out`

  OUTFILE=$DIR/$EVALFILE.out 
  ERRFILE=$DIR/$EVALFILE.err
  SETFILE=$DIR/$EVALFILE.set
  RESFILE=$DIR/$EVALFILE.res
  TEXFILE=$DIR/$EVALFILE.tex
  PAVFILE=$DIR/$EVALFILE.pav
  
  # check if the eval file exists; if this is the case construct the overall solution files
  if test -e $DIR/$EVALFILE.eval
  then
      echo > $OUTFILE
      echo > $ERRFILE
      echo create overall output and error file for $EVALFILE

      for i in `cat $DIR/$EVALFILE.eval` DONE
	do
	if test "$i" = "DONE"
	then
	    break
	fi
	
	FILE=$i.out
	if test -e $FILE
	then
	    cat $FILE >> $OUTFILE
	    if test "$REMOVE" = "1"
	    then
		rm -f $FILE
	    fi
	else
	    echo Missing $i
	fi
      
	FILE=$i.err
	if test -e $FILE
	then
	    cat $FILE >> $ERRFILE
	    if test "$REMOVE" = "1"
	    then
		rm -f $FILE
	    fi
	fi
      
	FILE=$i.set
	if test -e $FILE
	then
	    cp $FILE $SETFILE
	    if test "$REMOVE" = "1"
	    then
		rm -f $FILE
	    fi
	fi
      
	FILE=$i.tmp
	if test -e $FILE
        then
	    if test "$REMOVE" = "1"
	    then
		rm -f $FILE
	    fi
	fi
      done
      
      if test "$REMOVE" = "1"
      then
	  rm -f $DIR/$EVALFILE.eval
      fi
  fi

  # check if the out file exists
  if test -e $DIR/$EVALFILE.out
  then

      echo create results for $EVALFILE

      QUEUE=`echo $EVALFILE | sed 's/check.\([a-zA-Z0-9_-]*\).*/\1/g'`

      if test "$QUEUE" = "gbe"
      then
	  TSTNAME=`echo $EVALFILE | sed 's/check.gbe.\([a-zA-Z0-9_-]*\).*/\1/g'`
      else 
	  if test "$QUEUE" = "ib"
	  then
	      TSTNAME=`echo $EVALFILE | sed 's/check.ib.\([a-zA-Z0-9_-]*\).*/\1/g'`
	  else
	      TSTNAME=$QUEUE
	  fi
      fi

      echo $QUEUE
      echo $TSTNAME

      if test -f $TSTNAME.test
      then
	  TESTFILE=$TSTNAME.test
      else
	  TESTFILE=""
      fi

      if test -f $TSTNAME.solu
      then
	  SOLUFILE=$TSTNAME.solu
      else 
	  if test -f all.solu
	  then
	      SOLUFILE=all.solu
	  else
	      SOLUFILE=""
	  fi
      fi

      awk -f check.awk -v "TEXFILE=$TEXFILE" -v "PAVFILE=$PAVFILE" $AWKARGS $TESTFILE $SOLUFILE $OUTFILE | tee $RESFILE
  fi
done
