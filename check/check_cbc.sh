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

TSTNAME=$1
CBCBIN=$2
SETNAME=$3
BINNAME=$CBCBIN.$4
TIMELIMIT=$5
NODELIMIT=$6
MEMLIMIT=$7
FEASTOL=$8
MIPGAP=$9
CONTINUE=${10}

if test ! -e results
then
    mkdir results
fi
if test ! -e settings
then
    mkdir settings
fi

OUTFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.out
ERRFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.err
RESFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.res
TEXFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.tex
TMPFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.tmp
SETFILE=results/check.$TSTNAME.$BINNAME.$SETNAME.cmd

SETTINGS=settings/$SETNAME.cbcset

if test "$CONTINUE" = "true"
then
    MVORCP=cp
else
    MVORCP=mv
fi

DATEINT=`date +"%s"`
if test -e $OUTFILE
then
    $MVORCP $OUTFILE $OUTFILE.old-$DATEINT
fi
if test -e $ERRFILE
then
    $MVORCP $ERRFILE $ERRFILE.old-$DATEINT
fi

if test "$CONTINUE" = "true"
then
    LASTPROB=`getlastprob.awk $OUTFILE`
    echo Continuing benchmark. Last solved instance: $LASTPROB
    echo "" >> $OUTFILE
    echo "----- Continuing from here. Last solved: $LASTPROB -----" >> $OUTFILE
    echo "" >> $OUTFILE
else
    LASTPROB=""
fi

uname -a >>$OUTFILE
uname -a >>$ERRFILE
date >>$OUTFILE
date >>$ERRFILE

HARDTIMELIMIT=`echo $TIMELIMIT*1.1 | bc`
HARDMEMLIMIT=`echo $MEMLIMIT*1.2 | bc`
echo hard time limit: $HARDTIMELIMIT >>$OUTFILE
echo hard mem limit: $HARDMEMLIMIT >>$OUTFILE

for i in `cat $TSTNAME.test`
do
    if test "$LASTPROB" = ""
    then
	LASTPROB=""
	if test -f $i
	then
	    rm -f $SETFILE
	    echo @01 $i ===========
	    echo @01 $i ===========                 >> $ERRFILE
	    echo @03 SETTINGS: $SETNAME
	    if test $SETNAME != "default"
	    then
		cp $SETTINGS $TMPFILE
	    else
		echo ""                              > $TMPFILE
	    fi
	    if test $FEASTOL != "default"
	    then
		echo primalTolerance $FEASTOL       >> $TMPFILE
		echo integerTolerance $FEASTOL      >> $TMPFILE
	    fi
	    echo seconds $TIMELIMIT                 >> $TMPFILE
	    if test $MIPGAP != "default"
	    then
		echo ratioGap $MIPGAP               >> $TMPFILE
	    fi
	    echo maxNodes $NODELIMIT                >> $TMPFILE
	    echo import $i                          >> $TMPFILE
	    echo ratioGap                           >> $TMPFILE
	    echo allowableGap                       >> $TMPFILE
	    echo seconds                            >> $TMPFILE
	    echo stat                               >> $TMPFILE
	    echo solve                              >> $TMPFILE
	    echo quit                               >> $TMPFILE
	    cp $TMPFILE $SETFILE
	    echo -----------------------------
	    date
	    echo -----------------------------
	    tcsh -c "limit cputime $HARDTIMELIMIT s; limit memoryuse $HARDMEMLIMIT M; limit filesize 1000 M; $CBCBIN < $TMPFILE" 2>>$ERRFILE
	    echo -----------------------------
	    date
	    echo -----------------------------
	    echo =ready=
	else
	    echo @02 FILE NOT FOUND: $i ===========
	    echo @02 FILE NOT FOUND: $i =========== >>$ERRFILE
	fi
    else
	echo skipping $i
	if test "$LASTPROB" = "$i"
	then
	    LASTPROB=""
        fi
    fi
done | tee -a $OUTFILE

rm -f $TMPFILE

date >>$OUTFILE
date >>$ERRFILE

if test -f $TSTNAME.solu
then
    gawk -f check_cbc.awk -vTEXFILE=$TEXFILE $TSTNAME.solu $OUTFILE | tee $RESFILE
else
    gawk -f check_cbc.awk -vTEXFILE=$TEXFILE $OUTFILE | tee $RESFILE
fi
