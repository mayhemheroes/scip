#!/usr/bin/env bash
# 
# This scripts generates the dependences for SCIP 
#

LPSS=(cpx spx spx2 xprs msk clp grb qso none)
OPTS=(opt dbg prf opt-gccold)
TPIS=(omp tny none)
EXPRINTS=(none cppad)

for TPI in ${TPIS[@]}
do
   for OPT in ${OPTS[@]}
   do
      # dependencies of main SCIP source and objscip library
      # with ZIMPL disabled
      make OPT=$OPT ZIMPL=false LPS=none TPI=$TPI scipdepend
      make OPT=$OPT ZIMPL=false LPS=none TPI=$TPI tpidepend

      # dependencies of cmain and cppmain
      make OPT=$OPT ZIMPL=false LPS=none TPI=$TPI LINKER=C   maindepend
      make OPT=$OPT ZIMPL=false LPS=none TPI=$TPI LINKER=CPP maindepend

      for LPS in ${LPSS[@]}
      do
         # check if the header for the LP solver are available,
         # or we are in the special case "none"
         # in the case "qso", the include directory is called qsinc
         if [ -e lib/include/$LPS"inc" ] || [ "$LPS" == "none" ] || [ "$LPS" == "spx2" -a -e lib/include/spxinc ] || [ "$LPS" == "qso" -a -e lib/include/qsinc ] || [ "$LPS" == "clp" -a -e lib/clp.*.opt ]
         then
         make LPS=$LPS OPT=$OPT TPI=$TPI lpidepend
         fi
      done

      # dependencies of nlpi libraries
      for EXPRINT in ${EXPRINTS[@]}
      do
         if test "$EXPRINT" == "none" -o "$EXPRINT" == "cppad" -o -e lib/include/$EXPRINT -o -e lib/include/$EXPRINT"inc"
         then
            make OPT=$OPT LPS=none TPI=$TPI EXPRINT=$EXPRINT IPOPT=false nlpidepend

            for ipoptopt in opt dbg
            do
               for libtype in shared static
               do
                  if ls lib/$libtype/ipopt.*.$ipoptopt > /dev/null 2>&1;
                  then
                     shared=`[ $libtype = "shared" ] && echo true || echo false`
                     make OPT=$OPT LPS=none TPI=$TPI EXPRINT=$EXPRINT IPOPT=true SHARED=$shared IPOPTOPT=$ipoptopt nlpidepend
                  fi
               done
            done
         fi
      done
   done
done
