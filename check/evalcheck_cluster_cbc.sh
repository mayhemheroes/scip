#!/bin/bash
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2008 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id: evalcheck_cluster_cbc.sh,v 1.2 2008/08/25 12:25:47 bzfwanie Exp $
export LANG=C

AWKARGS=""
FILES=""
DIR=`dirname $1`
EVALFILE=`basename $1 .eval`

OUTFILE=$DIR/$EVALFILE.out 
RESFILE=$DIR/$EVALFILE.res
TEXFILE=$DIR/$EVALFILE.tex
PAVFILE=$DIR/$EVALFILE.pav

echo > $OUTFILE
echo create overall output file
for i in `cat $DIR/$EVALFILE.eval` DONE
  do
  if test "$i" = "DONE"
      then
      break
  fi
  
  if test -e $i
      then
      cat $i >> $OUTFILE
  fi
done


TSTNAME=`echo $EVALFILE | sed 's/check.\([a-zA-Z0-9_]*\).*/\1/g'`

if test -f $TSTNAME.test
    then
    TESTFILE=$TSTNAME.test
else
    TESTFILE=""
fi

if test -f $TSTNAME.solu
    then
    SOLUFILE=$TSTNAME.solu
else if test -f all.solu
    then
    SOLUFILE=all.solu
else
    SOLUFILE=""
fi
fi
gawk -f check_cbc.awk -v "TEXFILE=$TEXFILE" -v "PAVFILE=$PAVFILE" $AWKARGS $TESTFILE $SOLUFILE $OUTFILE | tee $RESFILE

