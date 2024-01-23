/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2022 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scipopt.org.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   conflict_resolution.c
 * @ingroup OTHER_CFILES
 * @brief  methods and datastructures for generalized resolution-based conflict
 * analysis
 * @author Gioni Mexi
 *
 * @todo Description of the algorithm
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
// #define SCIP_DEBUG
// #define SCIP_MORE_DEBUG

#include "blockmemshell/memory.h"
#include "scip/conflict_resolution.h"
#include "scip/conflict_graphanalysis.h"
#include "scip/conflict_dualproofanalysis.h"
#include "scip/clock.h"
#include "scip/conflict.h"
#include "scip/cons.h"
#include "scip/cons_linear.h"
#include "scip/cuts.h"
#include "scip/history.h"
#include "scip/prob.h"
#include "scip/prop.h"
#include "scip/pub_conflict.h"
#include "scip/pub_cons.h"
#include "scip/pub_lp.h"
#include "scip/pub_misc.h"
#include "scip/pub_var.h"
#include "scip/scip_conflict.h"
#include "scip/scip_cons.h"
#include "scip/scip_prob.h"
#include "scip/scip_mem.h"
#include "scip/scip_sol.h"
#include "scip/scip_var.h"
#include "scip/set.h"
#include "scip/sol.h"
#include "scip/struct_conflict.h"
#include "scip/struct_lp.h"
#include "scip/struct_set.h"
#include "scip/struct_stat.h"
#include "scip/tree.h"
#include "scip/var.h"

#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#else
#include <strings.h> /*lint --e{766}*/
#endif

#define EPS                       1e-06

// #define SCIP_CONFGRAPH_DOT

#ifdef SCIP_CONFGRAPH_DOT
/*
 * Output of Conflict Graph
 */

#include <stdio.h>
#include "scip/scip_message.h"

static FILE*             confgraphfile = NULL;              /**< output file for current conflict graph */
static int               nsubgraphs = 0;                    /**< number of subgraphs in current conflict graph */
static int               nodeid = 0;                        /**< id of next node to add to the conflict graph */

static const char* colors[] = {
      "#FFCCCC",  /* conflict row */
      "#CCFFFF",  /* reason row */
      "#CCFFCC",  /* reduced reason row */
      "#F9EBEA",  /* resolved row row */
      "#3399FF",  /* current bound change */
      "#FF99FF"   /* next bound change */

   };
typedef enum {
   CONFLICT_ROW,
   REASON_ROW,
   REDUCED_REASON_ROW,
   RESOLVED_ROW,
   CURRENT_BOUND_CHANGE,
   NEXT_BOUND_CHANGE
} Color;

/** writes a node section to the conflict graph file */
static
void confgraphWriteNode(
   const char*           label,              /**< label of the node */
   const char*           nodetype,           /**< type of the node */
   const char*           fillcolor,          /**< color of the node's interior */
   const char*           bordercolor         /**< color of the node's border */
   )
{
   assert(confgraphfile != NULL);

   SCIPdotWriteNode(confgraphfile, nodeid, label, nodetype, fillcolor, bordercolor); /*lint !e571*/
   nodeid++;
}

/** creates a file to output the current conflict graph into; adds the conflict vertex to the graph */
static
SCIP_RETCODE confgraphCreate(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   char fname[SCIP_MAXSTRLEN];

   assert(conflict != NULL);
   assert(confgraphfile == NULL);

   (void) SCIPsnprintf(fname, SCIP_MAXSTRLEN, "conf%p%d.dot", conflict, conflict->count);

   SCIPinfoMessage(set->scip, NULL, "storing conflict graph in file <%s>\n", fname);

   confgraphfile = fopen(fname, "w");

   if( confgraphfile == NULL )
   {
      SCIPerrorMessage("cannot open graph file <%s>\n", fname);
      SCIPABORT(); /*lint !e527*/
      return SCIP_WRITEERROR;
   }

   SCIPdotWriteOpening(confgraphfile);

   confgraphWriteNode("conflict", "plaintext", "#ff0000", "#000000");
   fprintf(confgraphfile, "\trankdir=LR; \n");


   return SCIP_OKAY;
}

/** closes conflict graph file */
static
void confgraphFree(

   void
   )
{
   if( confgraphfile != NULL )
   {
      SCIPdotWriteClosing(confgraphfile);

      fclose(confgraphfile);

      confgraphfile = NULL;
      nsubgraphs = 0;
   }
}

/** adds a bound change node to the conflict graph */
static
void confgraphAddBdchg(
   SCIP_BDCHGINFO*       bdchginfo,          /**< bound change to add to the conflict graph */
   int                   col                 /**< color of the node */
   )
{
   char label[SCIP_MAXSTRLEN];
   int offset;

   offset = 0;
   if (col == 4)
      offset = SCIPsnprintf(label, SCIP_MAXSTRLEN, "Bound to resolve:\n");
   else
   {
      assert(col == 5);
      offset = SCIPsnprintf(label, SCIP_MAXSTRLEN, "Next bound in queue:\n");
   }

   (void) SCIPsnprintf(label + offset, SCIP_MAXSTRLEN - offset, "x_%d %s %g\n[%d:%d] %s",
      SCIPvarGetProbindex(SCIPbdchginfoGetVar(bdchginfo)),
      SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
      SCIPbdchginfoGetNewbound(bdchginfo), SCIPbdchginfoGetDepth(bdchginfo), SCIPbdchginfoGetPos(bdchginfo),
      SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_BRANCHING ? "branch"
      : (SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_CONSINFER
         ? SCIPconsGetName(SCIPbdchginfoGetInferCons(bdchginfo))
         : (SCIPbdchginfoGetInferProp(bdchginfo) != NULL ? SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo))
            : "none")));
   confgraphWriteNode(label, "ellipse", colors[col], "#000000");
}

/** adds a row node to the conflict graph */
static
void confgraphAddRow(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   int                   col                 /**< color of the node */
   )
{
   int MAXLEN = 1048576;
   char label[MAXLEN];
   int offset = 0;
   int i = 0;
   int elementsinoneline = 5;

   if(col == 0 || col == 3)
   {
      SCIP_CONFLICTROW* conflictrow;
      conflictrow = conflict->conflictrow;
      if (col == 0)
         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "Conflict Row: \n");
      else
         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "Resolved Row: \n");
      for(i = 0; i < conflictrow->nnz; i++)
      {
         int v;
         v = conflictrow->inds[i];

         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "%f<x_%d> ", conflictrow->vals[v], v);
         if ((i + 1) % elementsinoneline == 0)
            offset += SCIPsnprintf(label + offset, MAXLEN - offset, "\n");
         if(offset >= MAXLEN - 1)
            break;
      }
      SCIPsnprintf(label + offset, MAXLEN - offset, "\n >= %f \n", conflictrow->lhs);
   }
   else
   {
      SCIP_REASONROW * reasonrow;
      reasonrow = conflict->reasonrow;
      if (col == 1)
         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "Reason Row: \n");
      else{
         assert(col == 2);
         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "Reduced Reason Row: \n");
      }
      for(i = 0; i < reasonrow->nnz; i++)
      {
         int v;
         SCIP_Real val;
         v = reasonrow->inds[i];
         val = reasonrow->vals[v];
         offset += SCIPsnprintf(label + offset, MAXLEN - offset, "%f<x_%d> ", val, v);
         if ((i + 1) % elementsinoneline == 0)
            offset += SCIPsnprintf(label + offset, MAXLEN - offset, "\n");
         if(offset >= MAXLEN - 1)
            break;
      }
      SCIPsnprintf(label + offset, MAXLEN - offset, "\n >= %f \n", reasonrow->lhs);

   }

   confgraphWriteNode(label, "plaintext", colors[col], "#000000");
}


/** start subgraph for current iteration */
static
void startSubgraph(
   int                   iteration           /**< current iteration of resolution */
   )
{
   fprintf(confgraphfile, "\tsubgraph cluster_%d { \n", nsubgraphs);
   fprintf(confgraphfile, "\tlabel = \"Iteration %d\"; \n", iteration);
   nsubgraphs++;
}

/** start subgraph for current iteration */
static
void endSubgraph(
   void
   )
{
   fprintf(confgraphfile, "\t } \n \n");
}

#endif

/** creates a copy of the given generalized resolution row, allocating an additional amount of memory */
static
SCIP_RETCODE conflictRowCopy(
   SCIP_CONFLICTROW**    targetrow,          /**< pointer to store the generalized resolution row */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_CONFLICTROW*     sourcerow           /**< source generalized resolution row */
   )
{
   int targetsize;
   int nvars;

   assert(targetrow != NULL);
   assert(sourcerow != NULL);

   targetsize = sourcerow->nnz;
   nvars = sourcerow->nvars;
   SCIP_ALLOC( BMSallocBlockMemory(blkmem, targetrow) );
   SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*targetrow)->inds, targetsize) );
   SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*targetrow)->vals, nvars) );

   /* copy all data from source to target */
   BMScopyMemoryArray((*targetrow)->inds, sourcerow->inds, targetsize);
   BMScopyMemoryArray((*targetrow)->vals, sourcerow->vals, nvars);

   /* copy all other data */
   (*targetrow)->lhs = sourcerow->lhs;
   (*targetrow)->slack = sourcerow->slack;
   (*targetrow)->coefquotient = sourcerow->coefquotient;
   (*targetrow)->nvars = nvars;
   (*targetrow)->nnz = targetsize;
   (*targetrow)->size = targetsize;
   (*targetrow)->validdepth = sourcerow->validdepth;
   (*targetrow)->conflictdepth = sourcerow->conflictdepth;
   (*targetrow)->repropdepth = sourcerow->repropdepth;
   (*targetrow)->insertdepth = sourcerow->insertdepth;
   (*targetrow)->usescutoffbound = sourcerow->usescutoffbound;
   (*targetrow)->isbinary = sourcerow->isbinary;
   (*targetrow)->conflicttype = sourcerow->conflicttype;

   return SCIP_OKAY;
}

/** replaces a generalized resolution row by another; allocate an additional amount of memory if needed */
static
SCIP_RETCODE conflictRowReplace(
   SCIP_CONFLICTROW*     targetrow,          /**< pointer to store the generalized resolution row */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_CONFLICTROW*     sourcerow           /**< source generalized resolution row */
   )
{
   int sourcennzs;
   int targetsize;
   int nvars;

   assert(targetrow != NULL);
   assert(sourcerow != NULL);

   nvars = sourcerow->nvars;
   sourcennzs = sourcerow->nnz;
   targetsize = targetrow->size;

   /* allocate additional memory for the indices array if needed */
   if( targetsize < sourcennzs )
   {
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &targetrow->inds, targetsize, sourcennzs) );
   }

   /* copy all data from source to target */
   BMScopyMemoryArray(targetrow->inds, sourcerow->inds, sourcennzs);
   BMScopyMemoryArray(targetrow->vals, sourcerow->vals, nvars);

   targetrow->lhs = sourcerow->lhs;
   targetrow->slack = sourcerow->slack;
   targetrow->coefquotient = sourcerow->coefquotient;
   targetrow->nvars = sourcerow->nvars;
   targetrow->nnz = sourcennzs;
   targetrow->size = MAX(sourcennzs, targetsize);
   targetrow->validdepth = sourcerow->validdepth;
   targetrow->conflictdepth = sourcerow->conflictdepth;
   targetrow->repropdepth = sourcerow->repropdepth;
   targetrow->insertdepth = sourcerow->insertdepth;
   targetrow->usescutoffbound = sourcerow->usescutoffbound;
   targetrow->isbinary = sourcerow->isbinary;
   targetrow->conflicttype = sourcerow->conflicttype;

   return SCIP_OKAY;
}

/** resizes conflict rows array to be able to store at least num entries */
static
SCIP_RETCODE conflictEnsureConflictRowsMem(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   num                 /**< minimal number of slots in array */
   )
{
   assert(conflict != NULL);
   assert(set != NULL);

   if( num > conflict->conflictrowssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      SCIP_ALLOC( BMSreallocMemoryArray(&conflict->conflictrows, newsize) );
      conflict->conflictrowssize = newsize;
   }
   assert(num <= conflict->conflictrowssize);

   return SCIP_OKAY;
}

/** add conflict row to the array of all conflicts rows */
static
SCIP_RETCODE conflictInsertConflictRow(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICTROW**    conflictrow         /**< conflict row to add */
   )
{
   assert(conflict != NULL);
   assert(conflictrow != NULL);

   /* insert resolution into the conflictrows array */
   SCIP_CALL( conflictEnsureConflictRowsMem(conflict, set, conflict->nconflictrows + 1) );

   SCIPsetDebugMsgPrint(set, "inserting conflict row (valid depth: %d, conf depth: %d, reprop depth: %d):\n",
                   (*conflictrow)->validdepth, (*conflictrow)->conflictdepth, (*conflictrow)->repropdepth);

   conflict->conflictrows[conflict->nconflictrows] = *conflictrow;
   ++conflict->nconflictrows;

   /* ownership of pointer is now in the conflictrows array */
   *conflictrow = NULL;

   return SCIP_OKAY;
}

/** gets number of conflict constraints detected in resolution conflict analysis */
SCIP_Longint SCIPconflictGetNResConflictConss(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nresconfconss;
}

/** gets number of calls to resolution conflict analysis that yield at least one conflict constraint */
SCIP_Longint SCIPconflictGetNResSuccess(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nressuccess;
}


/** gets number of calls to resolution conflict analysis terminating because of large coefficients */
SCIP_Longint SCIPconflictGetNResLargeCoefs(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nreslargecoefs;
}

/** gets number of calls to resolution conflict analysis terminating because of long conflicts */
SCIP_Longint SCIPconflictGetNResLongConflicts(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nreslongconfs;
}

/** gets number of calls to resolution conflict analysis */
SCIP_Longint SCIPconflictGetNResCalls(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nrescalls;
}

/** gets the percentage of weeakening candidates that was actually weakened */
SCIP_Real SCIPconflictGetWeakeningPercentage(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);
   if ( conflict->nreductioncalls == 0 )
      return 0.0;
   else
      return 100.0 * conflict->weakeningsumperc / conflict->nreductioncalls;
}

/** gets number of calls that resolution conflict analysis stopped for an unknown reason */
SCIP_Longint SCIPconflictGetNResUnkTerm(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nrescalls - conflict->nressuccess - conflict->nknownaborts;
}

/** gets the number of unresolvable bound changes */
SCIP_Longint SCIPconflictGetNUnresolvable(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   assert(conflict != NULL);

   return conflict->nunresolvable;
}


#ifdef SCIP_DEBUG
static int               dbgelementsoneline = 5;               /**< elements on a single line when writing rows */

/* Enum definition for the types of rows */
typedef enum {
   INITIAL_CONFLICT_ROWTYPE,
   CONFLICT_ROWTYPE,
   RESOLVED_CONFLICT_ROWTYPE,
   C_MIR_CONFLICT_ROWTYPE,
   TIGHTENED_CONFLICT_ROWTYPE,
   CLAUSAL_CONFLICT_ROWTYPE,
   AFTER_FIXING_UNRESOLVABLE_BOUND_CHANGE_CONFLICT_ROWTYPE,
   POSTPROCESSED_CONFLICT_ROWTYPE,
   REASON_ROWTYPE,
   REDUCED_REASON_ROWTYPE,
   CLAUSAL_REASON_ROWTYPE,
   CONTINUOUS_REASON_ROWTYPE
} ConflictRowType;

/** prints a generalized resolution row in debug mode */
static
void printConflictRow(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row to print */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   int                   type                /**< row type */
   )
{
      int nnzs;
      int v;
      int i;

      assert(vars != NULL);

      switch(type)
      {
      case INITIAL_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Initial Conflict Row:  \n");
         break;
      case CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Conflict Row:  \n");
         break;
      case RESOLVED_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Resolved Conflict Row:  \n");
         break;
      case C_MIR_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "c-MIR Conflict Row:  \n");
         break;
      case TIGHTENED_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Tightened Conflict Row:  \n");
         break;
      case CLAUSAL_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Clausal Conflict Row:  \n");
         break;
      case AFTER_FIXING_UNRESOLVABLE_BOUND_CHANGE_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "After Fixing Unresolvable Bound Change Conflict Row:  \n");
         break;
      case POSTPROCESSED_CONFLICT_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Postprocessed Conflict Row:  \n");
         break;
      case REASON_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Reason Row:  \n");
         break;
      case REDUCED_REASON_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Reduced Reason Row:  \n");
         break;
      case CLAUSAL_REASON_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Clausal Reason Row:  \n");
         break;
      case CONTINUOUS_REASON_ROWTYPE:
         SCIPsetDebugMsgPrint(set, "Continuous Reason Row:  \n");
         break;
      default:
         SCIPsetDebugMsgPrint(set, "Some Row:  \n");
         break;
      }
      for( i = 0; i < row->nnz; i++ )
      {
         v = row->inds[i];
         assert(SCIPvarGetProbindex(vars[v]) == v);
         SCIPsetDebugMsgPrint(set, " %f<%s>", row->vals[v], SCIPvarGetName(vars[v]));
         if ((i + 1) % dbgelementsoneline == 0)
            SCIPsetDebugMsgPrint(set, "\n");
      }
      SCIPsetDebugMsgPrint(set, " >= %f\n", row->lhs);

      /* just to check there are no other nonzeros in the dense array */
      nnzs = 0;
      for( i = 0; i < row->nvars; i++ )
      {
         if(!SCIPsetIsZero(set, row->vals[i]))
         {
            nnzs++;
         }
      }
      assert(nnzs == row->nnz);
}

/** print a single bound change in debug mode
*/
static
void printSingleBoundChange(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change to print */
)
{
      SCIP_VAR* var;
      var = SCIPbdchginfoGetVar(bdchginfo);
      SCIPsetDebugMsgPrint(set, " \t -> Bound change <%s> %s %.15g [status:%d, type:%d, depth:%d, pos:%d, reason:<%s>, global bounds:[%.15g,%.15g], local bounds:[%.15g,%.15g]] \n",
      SCIPvarGetName(var),
      SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
      SCIPbdchginfoGetNewbound(bdchginfo),
      SCIPvarGetStatus(var), SCIPvarGetType(var),
      SCIPbdchginfoGetDepth(bdchginfo), SCIPbdchginfoGetPos(bdchginfo),
      SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_BRANCHING ? "branch"
      : (SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_CONSINFER
         ? SCIPconsGetName(SCIPbdchginfoGetInferCons(bdchginfo))
         : (SCIPbdchginfoGetInferProp(bdchginfo) != NULL ? SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo))
            : "none")),
            SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var),
            SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var));
}
/** prints all bound changes in the queue in debug mode
 */
static
void printAllBoundChanges(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Bool             continuousbdchgqueue/**< print the continuous bdchg queue */
   )
{
   SCIP_PQUEUE* bdchgqueue;
   SCIP_BDCHGINFO* bdchginfo;
   SCIP_VAR* var;
   int i;

   assert(conflict != NULL);

   bdchgqueue = continuousbdchgqueue ? conflict->continuousbdchgqueue : conflict->resbdchgqueue;

   SCIPsetDebugMsgPrint(set, "Bound changes in %s bound change queue: \n", continuousbdchgqueue ? "continuous" : "resolution");

   if( SCIPpqueueNElems(bdchgqueue) == 0 )
   {
      SCIPsetDebugMsgPrint(set, " \t -> The bound change queue is empty\n");
      return;
   }
   for( i = 0; i < SCIPpqueueNElems(bdchgqueue); ++i )
   {
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(bdchgqueue)[i]);
      var = bdchginfo->var;
      SCIPsetDebugMsgPrint(set, " \t -> Bound change %d: <%s> %s %.15g [status:%d, type:%d, depth:%d, pos:%d, reason:<%s>, global bounds:[%.15g,%.15g], local bounds:[%.15g,%.15g]] \n",
      i, SCIPvarGetName(var),
      SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
      SCIPbdchginfoGetNewbound(bdchginfo),
      SCIPvarGetStatus(var), SCIPvarGetType(var),
      SCIPbdchginfoGetDepth(bdchginfo), SCIPbdchginfoGetPos(bdchginfo),
      SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_BRANCHING ? "branch"
      : (SCIPbdchginfoGetChgtype(bdchginfo) == SCIP_BOUNDCHGTYPE_CONSINFER
         ? SCIPconsGetName(SCIPbdchginfoGetInferCons(bdchginfo))
         : (SCIPbdchginfoGetInferProp(bdchginfo) != NULL ? SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo))
            : "none")),
         SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var),
         SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var));
   }
   SCIPsetDebugMsgPrint(set, "End of bound changes in queue. \n");
}

/* print the type of the non resolvable reason in debug mode */
static
void printNonResolvableReasonType(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change to check */
   )
{
   SCIP_BOUNDCHGTYPE bdchgtype;

   bdchgtype = SCIPbdchginfoGetChgtype(bdchginfo);
   if (bdchgtype == SCIP_BOUNDCHGTYPE_BRANCHING)
   {
      SCIPsetDebugMsgPrint(set, " \t -> Not resolvable bound change: branching \n");
   }
   else if (bdchgtype == SCIP_BOUNDCHGTYPE_PROPINFER)
   {
      SCIP_PROP* reasonprop;
      reasonprop = SCIPbdchginfoGetInferProp(bdchginfo);

      SCIPsetDebugMsgPrint(set, " \t -> Not resolvable bound change: propagation %s \n",
      reasonprop != NULL ? SCIPpropGetName(reasonprop) : "none");
   }
   else
   {
      assert(bdchgtype == SCIP_BOUNDCHGTYPE_CONSINFER);
      SCIPsetDebugMsgPrint(set, " \t -> Not resolvable bound change: constraint %s \n", SCIPconsGetName(SCIPbdchginfoGetInferCons(bdchginfo)));
   }
}

#endif

/** perform activity based coefficient tightening on a semi-sparse or sparse row defined with a left hand side */
static
SCIP_RETCODE tightenCoefs(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_Bool             localbounds,        /**< do we use local bounds? */
   SCIP_Real*            rowcoefs,           /**< coefficients of the row */
   int*                  rowinds,            /**< indices of the variables in the row */
   int*                  rownnz,             /**< number of non-zeros in the row */
   SCIP_Real*            rowlhs,             /**< left hand side of the row */
   int*                  nchgcoefs,          /**< number of changed coefficients */
   SCIP_Bool             densevals,          /**< is the vector of values dense? */
   SCIP_Bool*            redundant           /**< pointer to store whether the row is redundant */
   )
{
   /* @todo slack update during coefficient tightening */
   int i;
   int nintegralvars;
   SCIP_Real* absvals;
   SCIP_Real QUAD(minacttmp);
   SCIP_Real minact;
   SCIP_Real maxabsval;

   assert(nchgcoefs != NULL);

   maxabsval = 0.0;
   QUAD_ASSIGN(minacttmp, 0.0);

   assert(vars != NULL);
   nintegralvars = SCIPgetNVars(set->scip) - SCIPgetNContVars(set->scip);
   SCIP_CALL_ABORT( SCIPallocBufferArray(set->scip, &absvals, *rownnz) );

   assert(nchgcoefs != NULL);
   *nchgcoefs = 0;

   if (redundant != NULL)
      *redundant = FALSE;

   /* refactortodo compute activity in a seperate method */
   for( i = 0; i < *rownnz; ++i )
   {
      SCIP_VAR* var;
      int idx;
      int coefidx;

      idx = rowinds[i];
      var = vars[idx];
      if(densevals)
         coefidx = idx;
      else
         coefidx = i;

      assert(idx >= 0 && idx < SCIPgetNVars(set->scip));
      assert(var != NULL);

      if( rowcoefs[coefidx] > 0.0 )
      {
         SCIP_Real lb = localbounds ? SCIPvarGetLbLocal(var) : SCIPvarGetLbGlobal(var);

         if( SCIPsetIsInfinity(set, -lb) )
            goto TERMINATE_TIGHTENING;

         if( idx < nintegralvars )
         {
            maxabsval = MAX(maxabsval, rowcoefs[coefidx]);
            absvals[i] = rowcoefs[coefidx];
         }
         else
            absvals[i] = 0.0;

         SCIPquadprecSumQD(minacttmp, minacttmp, lb * rowcoefs[coefidx]);
      }
      else
      {
         SCIP_Real ub = localbounds ? SCIPvarGetUbLocal(var) : SCIPvarGetUbGlobal(var);

         if( SCIPsetIsInfinity(set, ub) )
            goto TERMINATE_TIGHTENING;

         if( idx < nintegralvars )
         {
            maxabsval = MAX(maxabsval, -rowcoefs[coefidx]);
            absvals[i] = -rowcoefs[coefidx];
         }
         else
            absvals[i] = 0.0;

         SCIPquadprecSumQD(minacttmp, minacttmp, ub * rowcoefs[coefidx]);
      }
   }

   minact = QUAD_TO_DBL(minacttmp);

   /* row is redundant if minact is infinity */
   if (SCIPsetIsInfinity(set, minact) )
   {
      if (redundant != NULL)
         *redundant = TRUE;
      goto TERMINATE_TIGHTENING;
   }
   /* no coefficients can be tightened */
   if (SCIPsetIsInfinity(set, -minact) )
   {
      goto TERMINATE_TIGHTENING;
   }

   /* propagating constraint cannot be redundant */
   assert(!SCIPsetIsGE(set, minact, *rowlhs));

   /* terminate, because coefficient tightening cannot be performed; also
   excludes the case in which no integral variable is present */
   /* for lhs terminate if minact + maxabsval < rowlhs */
   if( SCIPsetIsLT(set, minact + maxabsval, *rowlhs) )
      goto TERMINATE_TIGHTENING;

   if(densevals)
      SCIPsortDownRealInt(absvals, rowinds, *rownnz);
   else
      SCIPsortDownRealRealInt(absvals, rowcoefs, rowinds, *rownnz);

   SCIPfreeBufferArray(set->scip, &absvals);

   /* loop over the integral variables and try to tighten the coefficients; see cons_linear for more details */
   for( i = 0; i < *rownnz; ++i )
   {
      SCIP_VAR* var;
      int idx;
      int coefidx;

      idx = rowinds[i];
      var = vars[idx];
      if(densevals)
         coefidx = idx;
      else
         coefidx = i;

      assert(idx >= 0 && idx < SCIPgetNVars(set->scip));
      assert(var != NULL);

      /* due to sorting, we can exit if we reached a continuous variable: all further integral variables have 0 coefficents anyway */
      if( idx >= nintegralvars )
         break;

      assert(SCIPvarIsIntegral(var));

      if( rowcoefs[coefidx] < 0.0 && SCIPsetIsGE(set, minact - rowcoefs[coefidx], *rowlhs) )
      {
         SCIP_Real newcoef = minact - (*rowlhs);
         SCIP_Real ub = localbounds ? SCIPvarGetUbLocal(var) : SCIPvarGetUbGlobal(var);

         assert(SCIPsetIsGE(set, newcoef + EPS, rowcoefs[coefidx]) || SCIPsetIsRelGE(set, newcoef, rowcoefs[coefidx]));
         assert(!SCIPsetIsPositive(set, newcoef));

         if( newcoef > rowcoefs[coefidx] )
         {
            SCIP_Real QUAD(delta);
            SCIP_Real QUAD(newlhs);

            SCIPquadprecSumDD(delta, newcoef, -rowcoefs[coefidx]);
            SCIPquadprecProdQD(delta, delta, ub);

            SCIPquadprecSumQD(newlhs, delta, *rowlhs);
            SCIPdebugPrintf("tightened coefficient from %g to %g; lhs changed from %g to %g; the bounds are [%g,%g]\n",
               rowcoefs[coefidx], newcoef, (*rowlhs), QUAD_TO_DBL(newlhs),
               localbounds ? SCIPvarGetUbLocal(var) : SCIPvarGetUbGlobal(var), ub);

            *rowlhs = QUAD_TO_DBL(newlhs);

            ++(*nchgcoefs);

            if( SCIPsetIsNegative(set, newcoef) )
            {
               SCIPquadprecSumQQ(minacttmp, minacttmp, delta);
               minact = QUAD_TO_DBL(minacttmp);
               rowcoefs[coefidx] = newcoef;
            }
            else
            {
               --(*rownnz);
               if(densevals)
                  rowcoefs[coefidx] = 0.0;
               else
                  rowcoefs[coefidx] = rowcoefs[*rownnz];

               rowinds[i] = rowinds[*rownnz];
               continue;
            }
         }
      }
      else if( rowcoefs[coefidx] > 0.0 && SCIPsetIsGE(set, minact + rowcoefs[coefidx], *rowlhs) )
      {
         SCIP_Real newcoef = (*rowlhs) - minact;
         SCIP_Real lb = localbounds ? SCIPvarGetLbLocal(var) : SCIPvarGetLbGlobal(var);

         assert(SCIPsetIsLE(set, newcoef, rowcoefs[coefidx] + EPS) || SCIPsetIsRelLE(set, newcoef, rowcoefs[coefidx]));
         assert(!SCIPsetIsNegative(set, newcoef));

         if( newcoef < rowcoefs[coefidx] )
         {
            SCIP_Real QUAD(delta);
            SCIP_Real QUAD(newlhs);

            SCIPquadprecSumDD(delta, newcoef, -rowcoefs[coefidx]);
            SCIPquadprecProdQD(delta, delta, lb);

            SCIPquadprecSumQD(newlhs, delta, *rowlhs);
            SCIPdebugPrintf("tightened coefficient from %g to %g; lhs changed from %g to %g; the bounds are [%g,%g]\n",
               rowcoefs[coefidx], newcoef, (*rowlhs), QUAD_TO_DBL(newlhs), lb,
               localbounds ? SCIPvarGetUbLocal(var) : SCIPvarGetUbGlobal(var));

            *rowlhs = QUAD_TO_DBL(newlhs);

            ++(*nchgcoefs);

            if( SCIPsetIsPositive(set, newcoef) )
            {
               SCIPquadprecSumQQ(minacttmp, minacttmp, delta);
               minact = QUAD_TO_DBL(minacttmp);
               rowcoefs[coefidx] = newcoef;
            }
            else
            {
               --(*rownnz);
               if(densevals)
                  rowcoefs[coefidx] = 0.0;
               else
                  rowcoefs[coefidx] = rowcoefs[*rownnz];

               rowinds[i] = rowinds[*rownnz];
               continue;
            }
         }
      }
      else /* due to sorting we can stop completely if the precondition was not fulfilled for this variable */
         break;
   }

  TERMINATE_TIGHTENING:
   SCIPfreeBufferArrayNull(set->scip, &absvals);

   return SCIP_OKAY;
}

/* check if the generalized resolution row has a relaxation only variable */
static
SCIP_Bool hasRelaxationOnlyVar(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     row                 /**< generalized resolution row */
   )
{
   int i;

   assert(set != NULL);
   assert(vars != NULL);
   assert(row != NULL);

   for( i = 0; i < row->nnz; ++i )
   {
      SCIP_VAR* var;

      var = vars[row->inds[i]];
      assert(var != NULL);

      if( SCIPvarIsRelaxationOnly(var) )
         return TRUE;
   }
   return FALSE;
}

/* check if a generalized resolution row has only binary variables */
static
SCIP_Bool isBinaryConflictRow(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     row                 /**< generalized resolution row */
   )
{
   int i;

   assert(set != NULL);
   assert(vars != NULL);
   assert(row != NULL);

   for( i = 0; i < row->nnz; ++i )
   {
      SCIP_VAR* var;

      var = vars[row->inds[i]];
      assert(var != NULL);

      if( !SCIPvarIsBinary(var) )
         return FALSE;
   }
   return TRUE;
}

/* Removes a variable with zero coefficient in the generalized resolution row */
static
void conflictRowRemoveZeroVar(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   pos                 /**< position of variable in conflict row */
   )
{
   int idx;

   idx = row->inds[pos];

   assert(row != NULL);
   assert(pos >= 0 && pos < row->nnz);
   assert(SCIPsetIsZero(set, row->vals[idx]));

   --row->nnz;
   row->vals[idx] = 0.0;
   row->inds[pos] = row->inds[row->nnz];
}

/* Removes all variables with zero coefficient (< 1e-09) in the generalized resolution row */
static
void conflictRowRemoveZeroVars(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   int idx;
   assert(row != NULL);
   for( int i = row->nnz - 1; i >= 0; --i )
   {
      idx = row->inds[i];
      if( SCIPsetIsZero(set, row->vals[idx]) )
         conflictRowRemoveZeroVar(row, set, i);
   }
}

/** Sort the reason row so that indices with variable at global bounds are in the end of the array */
static
SCIP_RETCODE reasonRowSortWeakeningOrder(
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_BDCHGIDX*        currbdchgidx,       /**< current bound change index */
   int                   residx,             /**< index of the variable to resolve */
   int*                  nweakencands,       /**< pointer to store the number of candidates for weakening */
   SCIP_Bool             keepdivisiblecoefs, /**< whether we want to keep divisible coefficients */
   SCIP_Real             divisor             /**< the divisor of the row */
   )
{
   SCIP_Real* varscores;
   int i;
   int varidx;
   SCIP_Real largestcoef;
   assert(reasonrow != NULL);
   assert(set != NULL);
   assert(vars != NULL);

   SCIP_CALL( SCIPsetAllocBufferArray(set, &varscores, reasonrow->nnz) );

   /* compute largest coefficient */
   largestcoef = 0.0;
   for( i = 0; i < reasonrow->nnz; ++i )
   {
      varidx = reasonrow->inds[i];
      if( fabs(reasonrow->vals[varidx]) > largestcoef )
         largestcoef = fabs(reasonrow->vals[varidx]);
   }
   /* increase the largest coefficient by one to make sure the sorting will work */
   largestcoef += 1.0;

   /* - Variables that cannot be weakened get the highest score (largestcoef)
    * - Variables that can be weakened get a score based on their coefficient size
    * Finally, we sort the row in non-increasing order based on the score
    */
   for( i = 0; i < reasonrow->nnz; ++i )
   {
      SCIP_VAR* vartoweaken;
      SCIP_Real ub;
      SCIP_Real lb;

      varidx = reasonrow->inds[i];

      vartoweaken = vars[varidx];

      ub = SCIPgetVarUbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);
      lb = SCIPgetVarLbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);

      /* if keepdivisiblecoefs is true we do not want to weaken variables with coefficient divisible by the divisor */
      if ( keepdivisiblecoefs && SCIPsetIsZero(set, fmod(reasonrow->vals[varidx], divisor)) )
      {
         varscores[i] = largestcoef;
      }
      /* we do not want to weaken the variable we are resolving */
      else if ( SCIPvarGetProbindex(vartoweaken) == residx )
      {
         varscores[i] = largestcoef;
      }
      /* variables still at global bounds */
      else if( SCIPsetIsEQ(set, ub, SCIPvarGetUbGlobal(vartoweaken)) && SCIPsetIsEQ(set, lb, SCIPvarGetLbGlobal(vartoweaken)) )
      {
         (*nweakencands)++;
         varscores[i] = set->conf_weakeningorder == 'a' ? fabs(reasonrow->vals[varidx]): -fabs(reasonrow->vals[varidx]);
         if(set->conf_weakenfreefirst)
            varscores[i] += -largestcoef;
#ifdef SCIP_MORE_DEBUG
         SCIPsetDebugMsg(set, "Variable <%s> at global bounds with value %f and score %f\n", SCIPvarGetName(vartoweaken), reasonrow->vals[varidx], varscores[i]);
#endif
      }
      /* variables at the correct bound */
      else if( SCIPsetIsGT(set, reasonrow->vals[varidx], 0.0) && SCIPsetIsEQ(set, ub, SCIPvarGetUbGlobal(vartoweaken)) )
      {
         (*nweakencands)++;
         varscores[i] = set->conf_weakeningorder == 'a' ? fabs(reasonrow->vals[varidx]): -fabs(reasonrow->vals[varidx]);
#ifdef SCIP_MORE_DEBUG
         SCIPsetDebugMsg(set, "Variable <%s> at upper bound with value %f and score %f\n", SCIPvarGetName(vartoweaken), reasonrow->vals[varidx], varscores[i]);
#endif
      }
      /* variables at the correct bound */
      else if ( SCIPsetIsLT(set, reasonrow->vals[varidx], 0.0) && SCIPsetIsEQ(set, lb, SCIPvarGetLbGlobal(vartoweaken)) )
      {
         (*nweakencands)++;
         varscores[i] = set->conf_weakeningorder == 'a' ? fabs(reasonrow->vals[varidx]): -fabs(reasonrow->vals[varidx]);
#ifdef SCIP_MORE_DEBUG
         SCIPsetDebugMsg(set, "Variable <%s> at lower bound with value %f and score %f\n", SCIPvarGetName(vartoweaken), reasonrow->vals[varidx], varscores[i]);
#endif
      }
      /* variables at the wrong bound */
      else
         varscores[i] = largestcoef;

   }
   /* sort the reason row such that the variables with highest score are first */
   SCIPsortDownRealInt(varscores, reasonrow->inds, reasonrow->nnz);

   SCIPsetFreeBufferArray(set, &varscores);

   return SCIP_OKAY;
}

/** perform Normalized MIR with a divisor d > 0 on a constraint lhs <= a^T x */
static
SCIP_RETCODE NormalizedMirLhs(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   SCIP_Real             divisor,            /**< the divisor of the row */
   SCIP_Bool*            cutfound            /**< whether a cut was found */
   )
{
   int varidx;

   SCIP_Real negcoefsum;
   SCIP_Real newlhs;

   SCIP_Real fraclhs;
   SCIP_Real normalizationtermlhs;
   // TODO: make this work also for continuous variables: Complement s.t. the bounds are positive
   assert(set != NULL);
   assert(vars != NULL);
   assert(reasonrow != NULL);
   assert(reasonrow->inds != NULL);
   assert(reasonrow->vals != NULL);
   assert(reasonrow->nnz > 0);

   assert(SCIPsetIsGT(set, divisor, 0.0));

   /* todo extend MIR for continuous and general integer variables */
   if (!isBinaryConflictRow(set, vars, reasonrow))
   {
      *cutfound = FALSE;
      return SCIP_OKAY;
   }

   SCIPsetDebugMsg(set, "MIR on constraint with LHS %f and divisor %f\n" , reasonrow->lhs, divisor);

   /* compute the sum of negative coefficients */
   negcoefsum = 0.0;
   for( int i = 0; i < reasonrow->nnz; ++i )
   {
      varidx = reasonrow->inds[i];
      if( SCIPsetIsLT(set, reasonrow->vals[varidx], 0.0) )
         negcoefsum += reasonrow->vals[varidx];
   }

   /* compute the new lhs (after normalization) */
   newlhs = reasonrow->lhs - negcoefsum;
   newlhs /= divisor;
   /* compute the fractionality of the lhs */
   fraclhs = newlhs - SCIPsetFloor(set, newlhs);

   /* since we only consider one divisor, it may happen that the fractionality
   of the lhs is zero. In this case MIR is not helpful since there is nothing to strengthen on the lhs */
   if( SCIPsetIsZero(set, fraclhs) )
   {
      *cutfound = FALSE;
      return SCIP_OKAY;
   }

   reasonrow->lhs = newlhs;

   /* compute the fractionality of the lhs */
   fraclhs = reasonrow->lhs - SCIPsetFloor(set, reasonrow->lhs);

   /* the new lhs cannot be zero (if it were zero the constraint is redundant and wouldn't have propagated) */
   assert(!SCIPsetIsZero(set, reasonrow->lhs));

   /*get the ceiling similar to the CG procedure */
   reasonrow->lhs = SCIPsetCeil(set, reasonrow->lhs);

   SCIPsetDebugMsg(set, "New lhs %f with fractionality %f\n" , reasonrow->lhs, fraclhs);

   normalizationtermlhs = 0.0;

   /* loop over all non zeros and divide each row element by the divisor */
   for( int i = 0; i < reasonrow->nnz; ++i )
   {
      SCIP_Real newcoef;
      SCIP_Real frac;

      varidx = reasonrow->inds[i];
      newcoef = reasonrow->vals[varidx] / divisor;
      frac = newcoef - SCIPsetFloor(set, newcoef);

      if( SCIPsetIsGE(set, newcoef, 0.0) )
      {
         if ( SCIPsetIsGE(set, frac, fraclhs) )
            reasonrow->vals[varidx] = SCIPsetCeil(set, newcoef);
         else
            reasonrow->vals[varidx] = SCIPsetFloor(set, newcoef) + frac / fraclhs;
      }
      else
      {
         if ( SCIPsetIsGE(set, 1 - frac, fraclhs) )
         {
            reasonrow->vals[varidx] = SCIPsetFloor(set, newcoef);
            /* update the normalization term */
            normalizationtermlhs += reasonrow->vals[varidx];
         }
         else
         {
            SCIP_Real negcoeffrac;
            negcoeffrac = 1 - frac;
            reasonrow->vals[varidx] = SCIPsetCeil(set, newcoef) - negcoeffrac / fraclhs;
            /* update the normalization term */
            normalizationtermlhs += reasonrow->vals[varidx];
         }
      }
   }
   /* add the normalization term to the lhs */
   reasonrow->lhs += normalizationtermlhs;

   /* remove variables with zero coefficient */
   conflictRowRemoveZeroVars(reasonrow, set);

   *cutfound = TRUE;

   return SCIP_OKAY;
}

/** complement and apply MIR to the reason constraint lhs <= a^T x
 *  We complement a variable x_i:
 *  if a_i > 0 and x_i is not fixed to 0
 *  if a_i < 0 and x_i is fixed to 1
*/
static
SCIP_RETCODE ComplementedMirLhs(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,             /**< dense array of indices of fixed variables */
   SCIP_BDCHGIDX*        currbdchgidx,       /**< current bound change index */
   int                   idxreason,          /**< index in the reason */
   SCIP_Real             divisor             /**< the divisor of the row */
   )
{
   int varidx;

   SCIP_Real deltaoldlhs;
   SCIP_Real deltanewlhs;
   SCIP_Real oldlhs;
   SCIP_Real newlhs;
   SCIP_Real fraclhs;

   assert(set != NULL);
   assert(vars != NULL);
   assert(reasonrow != NULL);
   assert(reasonrow->inds != NULL);
   assert(reasonrow->vals != NULL);
   assert(reasonrow->nnz > 0);

   assert(SCIPsetIsGT(set, divisor, 0.0));

   /* todo we can use MIR for general constraints */
   if (!isBinaryConflictRow(set, vars, reasonrow))
      return SCIP_OKAY;

   SCIPsetDebugMsgPrint(set, "Stronger MIR on constraint with LHS %f and divisor %f\n" , reasonrow->lhs, divisor);

   /* complement and apply MIR to the reason constraint lhs <= a^T x
    * say the idxreason is k, and a_k > 0 (so this reason constraint fixes x_k to 1).
    * Then we complement as follows:
    *   - if a_i > 0 and x_i is not fixed to 0, we complement it
    *   - if a_i < 0 and x_i is fixed to 1, we complement it
    * whenever we complement a variable x_i, we need to modify the lhs with -a_i
    * Then we compute the fractionality of the lhs f(lhs) and do the following:
    * The coefficient of ~x_i is going to be -a_i, which after division is going to be -a_i/divisor; and after MIR,
    * it becomes CEIL(-a_i / divisor) if f(-a_i/divisor) >= f(lhs) or FLOOR(-a_i / divisor)+f(-a_i/divisor)/f(lhs) otherwise.
    * Complementing this again (to go back to x_i) the new coefficient of x_i is going to be -CEIL(-a_i / divisor)
    * or FLOOR(-a_i / divisor)+f(-a_i/divisor)/f(lhs) otherwise. It is its going to
    * contribute the same amount to the lhs of the resulting constraint.
    * So we keep to lhs deltas, one for complementing in the original space, and another for complementing after we do C-G
    *
    * On the other hand, if a_k < 0 (so this reason constraint fixes x_k to 0), then we complement x_k (so modify the lhs by -a_k)
    * and then we are in the previous case. However, at the end, we need to complement back, which means that we modify the lhs by -1
    */

   /* first handle x_k and initialize lhs deltas */
   varidx = idxreason;
   if( reasonrow->vals[varidx] < 0.0 )
   {
     deltaoldlhs = -reasonrow->vals[varidx];
     deltanewlhs = -1.0;
     reasonrow->vals[varidx] = -1.0;
   }
   else
   {
     deltaoldlhs = 0.0;
     deltanewlhs = 0.0;
     reasonrow->vals[varidx] = 1.0;
   }

   /* compute the delta for the left hand side after complementation in order to apply MIR
    * In a second loop set the new coefficients for the other variables and compute the lhs delta after complementation */
   for( int i = 0; i < reasonrow->nnz; ++i )
   {
      varidx = reasonrow->inds[i];
      SCIP_VAR* currentvar;
      SCIP_Real coef;

      if( varidx == idxreason )
        continue;

      coef = reasonrow->vals[varidx];
      currentvar = vars[varidx];

      assert(SCIPvarIsBinary(currentvar));

      /* refactortodo carefully check this */
      if ( (coef > 0.0 && (SCIPgetVarUbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 && fixinds[varidx] != 1) ) ||
          (coef < 0.0 && (SCIPgetVarLbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 || fixinds[varidx] == -1 )) )
      {
        deltaoldlhs += -coef;
      }
   }
   oldlhs = reasonrow->lhs;
   newlhs = (oldlhs + deltaoldlhs) / divisor;
   fraclhs = newlhs - SCIPsetFloor(set, newlhs);

   /* set the new coefficients for the other variables and compute the lhs deltas */
   for( int i = 0; i < reasonrow->nnz; ++i )
   {
      varidx = reasonrow->inds[i];
      SCIP_VAR* currentvar;
      SCIP_Real newcoef;
      SCIP_Real coef;
      SCIP_Real fraccoef;

      if( varidx == idxreason )
        continue;

      coef = reasonrow->vals[varidx] / divisor;
      fraccoef = coef - SCIPsetFloor(set, coef);
      currentvar = vars[varidx];

      if ( (coef > 0.0 && (SCIPgetVarUbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 && fixinds[varidx] != 1) ) ||
          (coef < 0.0 && (SCIPgetVarLbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 || fixinds[varidx] == -1 )) )
      {
        if ((1.0 - fraccoef) >= fraclhs)
        {
           newcoef = -SCIPsetCeil(set, -coef);

           reasonrow->vals[varidx] = newcoef;
           deltanewlhs += newcoef;
        }
        else
        {
           newcoef = -SCIPsetFloor(set, -coef) - (1 - fraccoef) / fraclhs;

           reasonrow->vals[varidx] = newcoef;
           deltanewlhs += newcoef;
        }
      }
      else
      {
         if( fraccoef >= fraclhs )
           reasonrow->vals[varidx] = SCIPsetCeil(set, coef);
         else
           reasonrow->vals[varidx] = SCIPsetFloor(set, coef) + fraccoef / fraclhs;
      }
   }

   newlhs = SCIPsetCeil(set, newlhs) + deltanewlhs;
   reasonrow->lhs = newlhs;

   /* remove variables with zero coefficient. Loop backwards */
   conflictRowRemoveZeroVars(reasonrow, set);

   return SCIP_OKAY;
}

/** complement and apply Chvatal-Gomory to the reason constraint lhs <= a^T x
 *  We complement a variable x_i:
 *  if a_i > 0 and x_i is not fixed to 0
 *  if a_i < 0 and x_i is fixed to 1
*/
static
SCIP_RETCODE ComplementedChvatalGomoryLhs(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,             /**< dense array of indices of fixed variables */
   SCIP_BDCHGIDX*        currbdchgidx,       /**< current bound change index */
   int                   idxreason,          /**< index in the reason */
   SCIP_Real             divisor             /**< the divisor of the row */
   )
{
   int varidx;
   SCIP_Real deltaoldlhs;
   SCIP_Real deltanewlhs;
   SCIP_Real oldlhs;
   SCIP_Real newlhs;

   assert(set != NULL);
   assert(vars != NULL);
   assert(reasonrow != NULL);
   assert(reasonrow->inds != NULL);
   assert(reasonrow->vals != NULL);
   assert(reasonrow->nnz > 0);

   assert(SCIPsetIsGT(set, divisor, 0.0));

   /* todo extend Chvatal-Gomory for constraints with general integer variables */
   if (!isBinaryConflictRow(set, vars, reasonrow))
      return SCIP_OKAY;

   assert(vars != NULL);
   SCIPsetDebugMsgPrint(set, "Complemented Normalized Chvatal-Gomory on constraint with LHS %f and divisor %f\n" , reasonrow->lhs, divisor);

   /* complement and apply chvatal gomory to the reason constraint lhs <= a^T x
    * say the idxreason is k, and a_k > 0 (so this reason constraint fixes x_k to 1).
    * Then we complement as follows:
    *   - if a_i > 0 and x_i is not fixed to 0, we complement it
    *   - if a_i < 0 and x_i is fixed to 1, we complement it
    * whenever we complement a variable x_i, we need to modify the lhs with -a_i
    * The coefficient of ~x_i is going to be -a_i, which after division is going to be -a_i/divisor; and after C-G,
    * it becomes CEIL(-a_i / divisor).
    * Complementing this again (to go back to x_i) the new coefficient of x_i is going to be -CEIL(-a_i / divisor) and its going to
    * contribute -CEIL(-a_i / divisor) to the lhs of the resulting constraint.
    * So we keep to lhs deltas, one for complementing in the original space, and another for complementing after we do C-G
    *
    * On the other hand, if a_k < 0 (so this reason constraint fixes x_k to 0), then we complement x_k (so modify the lhs by -a_k)
    * and then we are in the previous case. However, at the end, we need to complement back, which means that we modify the lhs by -1
    */

   varidx = idxreason;
   /* first handle x_k and initialize lhs deltas */
   if( reasonrow->vals[varidx] < 0.0 )
   {
     deltaoldlhs = -reasonrow->vals[varidx];
     deltanewlhs = -1.0;
     reasonrow->vals[varidx] = -1.0;
   }
   else
   {
     deltaoldlhs = 0.0;
     deltanewlhs = 0.0;
     reasonrow->vals[varidx] = 1.0;
   }

   /* set the new coefficients for the other variables and compute the lhs deltas */
   for( int i = 0; i < reasonrow->nnz; ++i )
   {
      varidx = reasonrow->inds[i];
      SCIP_VAR* currentvar;
      SCIP_Real newcoef;
      SCIP_Real coef;

      if( i == idxreason )
        continue;

      coef = reasonrow->vals[varidx];
      currentvar = vars[reasonrow->inds[i]];

      assert(SCIPvarIsBinary(currentvar));

      if ( (coef > 0.0 && (SCIPgetVarUbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 && fixinds[reasonrow->inds[i]] != 1) ) ||
          (coef < 0.0 && (SCIPgetVarLbAtIndex(set->scip, currentvar, currbdchgidx, TRUE) > 0.5 || fixinds[reasonrow->inds[i]] == -1 )) )
      {
        newcoef = -SCIPsetCeil(set, -coef / divisor);

        deltaoldlhs += -coef;
        reasonrow->vals[varidx] = newcoef;
        deltanewlhs += newcoef;
      }
      else
      {
        newcoef = SCIPsetCeil(set, coef / divisor);
        reasonrow->vals[varidx] = newcoef;
      }
   }

   oldlhs = reasonrow->lhs;
   newlhs = SCIPsetCeil(set, (oldlhs + deltaoldlhs) / divisor) + deltanewlhs;
   reasonrow->lhs = newlhs;

   /* remove variables with zero coefficient. Loop backwards */
   conflictRowRemoveZeroVars(reasonrow, set);

   return SCIP_OKAY;
}

/* linear combination row1 = row1 + scale * row2 */
static
SCIP_RETCODE linearCombRows(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICTROW*     row1,               /**< first row (aggregated row) */
   SCIP_CONFLICTROW*     row2,               /**< second row */
   SCIP_Real             scale               /**< scale factor for second row */
   )
{
   int i;

   conflictRowRemoveZeroVars(row1, set);

   /* add conflict and reason conflict rows; */
   for (i = 0; i < row2->nnz; ++i)
   {
      int idx;
      idx = row2->inds[i];
      assert(idx >= 0);
      if(SCIPsetIsZero(set, row1->vals[idx]))
      {
         row1->inds[row1->nnz] = idx;
         row1->vals[idx] = scale * row2->vals[idx];
         row1->nnz++;
      }
      else
         row1->vals[idx] = row1->vals[idx] + scale * row2->vals[idx];
   }
   row1->lhs = row1->lhs + scale * row2->lhs;

   return SCIP_OKAY;
}

/* returns whether a bound change is resolvable or not */
static
SCIP_Bool bdchginfoIsResolvable(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change to check */
   )
{
   SCIP_CONSHDLR *conshdlr;
   SCIP_BOUNDCHGTYPE bdchgtype;
   const char* conshdlrname;

   bdchgtype = SCIPbdchginfoGetChgtype(bdchginfo);

   /* branching */
   if (bdchgtype == SCIP_BOUNDCHGTYPE_BRANCHING)
      return FALSE;
   /* propagation */
   else if (bdchgtype == SCIP_BOUNDCHGTYPE_PROPINFER)
   {
      /* todo also other propagators can be resolved */
      if (SCIPbdchginfoGetInferProp(bdchginfo) == NULL)
         return FALSE;
      else if( strcmp(SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo)), "pseudoobj") == 0 )
         return TRUE;
      else if( strcmp(SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo)), "vbounds") == 0 )
         return TRUE;
      else
         return FALSE;
   }
   /* constraint */
   assert(bdchgtype == SCIP_BOUNDCHGTYPE_CONSINFER);
   conshdlr = SCIPconsGetHdlr(SCIPbdchginfoGetInferCons(bdchginfo));
   conshdlrname = SCIPconshdlrGetName(conshdlr);

   if( strcmp(conshdlrname, "nonlinear") == 0 )
      return FALSE;

   return TRUE;
}

/** returns if the we can extract the reason bound changes leading to the
 * implication of bdchginfo by reverse propagation */
static
SCIP_Bool reasonIsLinearizable(
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change to check */
   )
{
   SCIP_BOUNDCHGTYPE bdchgtype;



   bdchgtype = SCIPbdchginfoGetChgtype(bdchginfo);
   if (bdchgtype == SCIP_BOUNDCHGTYPE_BRANCHING)
      return FALSE;
   else if (bdchgtype == SCIP_BOUNDCHGTYPE_PROPINFER)
   {
      if (SCIPbdchginfoGetInferProp(bdchginfo) == NULL)
         return FALSE;
      else if( strcmp(SCIPpropGetName(SCIPbdchginfoGetInferProp(bdchginfo)), "pseudoobj") == 0 )
         return TRUE;
      else
         return FALSE;
   }
   else
   {
      SCIP_CONSHDLR *conshdlr;
      const char* conshdlrname;
      assert(bdchgtype == SCIP_BOUNDCHGTYPE_CONSINFER);
      conshdlr = SCIPconsGetHdlr(SCIPbdchginfoGetInferCons(bdchginfo));
      conshdlrname = SCIPconshdlrGetName(conshdlr);

      if( strcmp(conshdlrname, "linear") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "setppc") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "logicor") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "knapsack") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "varbound") == 0 )
      {
         return FALSE;
      }
      else if( strcmp(conshdlrname, "orbisack") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "orbitope") == 0 )
      {
         return TRUE;
      }
      else if( strcmp(conshdlrname, "and") == 0 )
      {
         return TRUE;
      }
      if( strcmp(conshdlrname, "xor") == 0 )
      {
         return TRUE;
      }
      if( strcmp(conshdlrname, "or") == 0 )
      {
         return TRUE;
      }
      if( strcmp(conshdlrname, "bounddisjunction") == 0 )
      {
         return TRUE;
      }
   }
   return FALSE;
}

/** returns whether there exists a resolvable bound change or not */
static
SCIP_Bool existsResolvablebdchginfo(
   SCIP_CONFLICT*        conflict            /**< conflict analysis data */
   )
{
   SCIP_BDCHGINFO* bdchginfo;
   int i;
   /* loop through forced to resolve bound changes and check if there exists a resolvable bound change */
   for( i = 0; i < SCIPpqueueNElems(conflict->resforcedbdchgqueue); ++i )
   {
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->resforcedbdchgqueue)[i]);
      if (bdchginfoIsResolvable(bdchginfo))
         return TRUE;
   }
   /* loop through bound change and check if there exists a resolvable bound change */
   for( i = 0; i < SCIPpqueueNElems(conflict->resbdchgqueue); ++i )
   {
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->resbdchgqueue)[i]);
      if (bdchginfoIsResolvable(bdchginfo))
         return TRUE;
   }
   return FALSE;
}

/** returns whether the bound change contributes to the conflict row being infeasible
 * It contributes if:
 *  - the bound change is on an upper bound and the coefficient of the variable in the conflict row is
 *    greater than zero
 *  - the bound change is on a lower bound and the coefficient of the variable in the conflict row
 *    is less than zero.
 *  - the row does not exist yet (e.g. in the initialization of conflict analysis)
 */
static
SCIP_Bool bdchginfoUsedForConflict(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_BDCHGINFO*       bdchginfo,          /**< bound change to check */
   int                   initial             /**< whether we are in the initialization of conflict analysis */
   )
{
   SCIP_CONFLICTROW* conflictrow;
   SCIP_VAR* var;
   int idx;

   conflictrow = conflict->conflictrow;

   if (initial)
      return TRUE;
   else if (conflictrow->nnz == 0)
      return FALSE;
   var = SCIPbdchginfoGetVar(bdchginfo);
   idx = SCIPvarGetProbindex(var);

   if( (conflictrow->vals[idx] > 0 ) && (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER) )
      return TRUE;
   else if( (conflictrow->vals[idx] < 0 ) && (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER) )
      return TRUE;
   return FALSE;
}

/** returns next conflict analysis candidate from the candidate queue without removing it */
static
SCIP_BDCHGINFO* conflictFirstCand(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   int                   initial             /**< whether we are in the initialization of conflict analysis */
   )
{

   SCIP_BDCHGINFO* bdchginfo;

   assert(conflict != NULL);


   if( SCIPpqueueNElems(conflict->resforcedbdchgqueue) > 0 )
   {
      /* get next potential candidate */
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueFirst(conflict->resforcedbdchgqueue));

      /* check if this candidate is valid */
      if( bdchginfoIsInvalid(conflict, bdchginfo) || !bdchginfoUsedForConflict(conflict, bdchginfo, initial))
      {
         SCIP_VAR* var;

         var = SCIPbdchginfoGetVar(bdchginfo);

         SCIPsetDebugMsgPrint(set, " \t -> bound change info [%d:%d<%s> %s %g] is invalid -> pop it from the queue\n",
            SCIPbdchginfoGetDepth(bdchginfo),
            SCIPbdchginfoGetPos(bdchginfo),
            SCIPvarGetName(var),
            SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPbdchginfoGetNewbound(bdchginfo));

         /* pop the invalid bound change info from the queue */
         (void)(SCIPpqueueRemove(conflict->resforcedbdchgqueue));

         if( SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER )
         {
            var->conflictreslb = SCIP_REAL_MIN;
         }
         else
         {
            assert(SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER);
            var->conflictresub = SCIP_REAL_MAX;
         }
         /* call method recursively to get next conflict analysis candidate */
         bdchginfo = conflictFirstCand(set, conflict, initial);
      }
   }
   else
   {
      /* get next potential candidate */
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueFirst(conflict->resbdchgqueue));

      /* check if this candidate is valid */
      if( bdchginfo != NULL && !bdchginfoUsedForConflict(conflict, bdchginfo, initial) )
      {
         SCIP_VAR* var;

         var = SCIPbdchginfoGetVar(bdchginfo);

         SCIPsetDebugMsgPrint(set, "\t -> bound change info [%d:%d<%s> %s %g] is invalid -> pop it from the queue\n",
            SCIPbdchginfoGetDepth(bdchginfo),
            SCIPbdchginfoGetPos(bdchginfo),
            SCIPvarGetName(var),
            SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPbdchginfoGetNewbound(bdchginfo));

         /* pop the invalid bound change info from the queue */
         (void)(SCIPpqueueRemove(conflict->resbdchgqueue));

         if( SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER )
         {
            var->conflictreslb = SCIP_REAL_MIN;
         }
         else
         {
            assert(SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER);
            var->conflictresub = SCIP_REAL_MAX;
         }

         /* call method recursively to get next conflict analysis candidate */
         bdchginfo = conflictFirstCand(set, conflict, initial);
      }
   }
   assert(bdchginfo == NULL || !SCIPbdchginfoIsRedundant(bdchginfo));

   return bdchginfo;
}

/** removes and returns next conflict analysis candidate from the candidate queue */
static
SCIP_BDCHGINFO* conflictRemoveCand(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   int                   initial             /**< whether we are in the initialization of conflict analysis */
   )
{
   SCIP_BDCHGINFO* bdchginfo;

   assert(conflict != NULL);

   if( SCIPpqueueNElems(conflict->resforcedbdchgqueue) > 0 )
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueRemove(conflict->resforcedbdchgqueue));
   else
      bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueRemove(conflict->resbdchgqueue));

   assert(!SCIPbdchginfoIsRedundant(bdchginfo));

   /* if we have a candidate this one should be valid for the current conflict analysis */
   assert(bdchginfoUsedForConflict(conflict, bdchginfo, initial));

   return bdchginfo;
}

/** return TRUE if generalized resolution conflict analysis is applicable */
SCIP_Bool SCIPconflictResolutionApplicable(
   SCIP_SET*             set                 /**< global SCIP settings */
   )
{
   /* check, if generalized resolution conflict analysis is enabled */
  if( !set->conf_enable || !set->conf_usegeneralres )
      return FALSE;

   return TRUE;
}

/** creates a generalized resolution row */
static
SCIP_RETCODE conflictRowCreate(
   SCIP_CONFLICTROW**    row,                /**< generalized resolution row */
   BMS_BLKMEM*           blkmem              /**< block memory of transformed problem */
   )
{
   assert(row != NULL);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, row) );
   (*row)->vals = NULL;
   (*row)->inds = NULL;
   (*row)->lhs = 0.0;
   (*row)->slack = 0.0;
   (*row)->coefquotient = 0.0;
   (*row)->nvars = 0;
   (*row)->nnz = 0;
   (*row)->size = 0;
   (*row)->validdepth = 0;
   (*row)->conflictdepth = 0;
   (*row)->repropdepth = 0;
   (*row)->insertdepth = 0;
   (*row)->usescutoffbound = FALSE;
   (*row)->isbinary = FALSE;
   /* refactortodo add a conflict type */
   (*row)->conflicttype = SCIP_CONFTYPE_UNKNOWN;

   return SCIP_OKAY;
}


/** creates conflict and reason rows */
SCIP_RETCODE SCIPconflictInitRows(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem              /**< block memory of transformed problem */
   )
{
   assert(conflict != NULL);
   assert(blkmem != NULL);

   SCIP_CALL( conflictRowCreate(&conflict->conflictrow, blkmem) );
   SCIP_CALL( conflictRowCreate(&conflict->resolvedconflictrow, blkmem) );

   SCIP_CALL( conflictRowCreate(&conflict->reasonrow, blkmem) );
   SCIP_CALL( conflictRowCreate(&conflict->reducedreasonrow, blkmem) );

   return SCIP_OKAY;
}

/** frees a generalized resolution row */
void SCIPconflictRowFree(
   SCIP_CONFLICTROW**    row,                /**< conflict row */
   BMS_BLKMEM*           blkmem              /**< block memory */
   )
{
   assert(row != NULL);
   assert(*row != NULL);
   assert(blkmem != NULL);

   BMSfreeBlockMemoryArrayNull(blkmem, &(*row)->vals, (*row)->nvars);
   BMSfreeBlockMemoryArrayNull(blkmem, &(*row)->inds, (*row)->size);
   BMSfreeBlockMemory(blkmem, row);
   (*row) = NULL;
}

static
void freeConflictResources(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_Real*            cutcoefs,           /**< cut coefficients or NULL */
   int*                  cutinds,            /**< cut indices or NULL */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds             /**< dense array of indices of fixed variables */
   )
{
    if (set->conf_applycmirreason || set->conf_applycmir || set->conf_applysimplemir) {
        SCIPsetFreeBufferArray(set, &cutinds);
        SCIPsetFreeBufferArray(set, &cutcoefs);
    }

    SCIPsetFreeBufferArray(set, &fixinds);
    SCIPsetFreeBufferArray(set, &fixbounds);

    /* free all conflict rows */
    for (int i = 0; i < conflict->nconflictrows; i++) {
        SCIPconflictRowFree(&conflict->conflictrows[i], blkmem);
    }
    conflict->nconflictrows = 0;
}

/** resets the data structure of a generalized resolution row */
static
void conflictRowClear(
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   int                   nvars               /**< number of variables in the problem */
   )
{
   int i;
   assert(row != NULL);

   /* this is necesseary to avoid memory leaks if the number of variables in the problem changes */
   if(row->vals != NULL && row->nvars != nvars)
      BMSfreeBlockMemoryArrayNull(blkmem, &row->vals, row->nvars);
   if(row->vals == NULL)
      BMSallocBlockMemoryArray(blkmem, &row->vals, nvars );

   for(i = 0 ; i < nvars; ++i)
      row->vals[i] = 0.0;

   row->nvars = nvars;
   row->nnz = 0;
   row->lhs = 0.0;
   row->slack = 0.0;
   row->coefquotient = 0.0;
   row->validdepth = 0;
   row->conflictdepth = 0;
   row->repropdepth = 0;
   row->insertdepth = 0;
   row->conflicttype = SCIP_CONFTYPE_UNKNOWN;
   row->usescutoffbound = FALSE;
   row->isbinary = FALSE;
}

/** weaken variable in a generalized resolution row */
static
void weakenVarConflictRow(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             var,                /**< variable to weaken */
   int                   pos                 /**< position in array of indices */
   )
{
   int idx;

   assert(row != NULL);
   assert(var != NULL);
   assert(pos >= 0 && pos < row->nvars);

   idx = row->inds[pos];

   SCIPdebugMessage("weaken variable <%s> in the row \n", SCIPvarGetName(var));

   /* weaken with global upper bound */
   if( SCIPsetIsGT(set, row->vals[idx], 0.0) )
   {
      row->lhs -= row->vals[idx] * SCIPvarGetUbGlobal(var);
   }
   /* weaken with global lower bound */
   else
   {
      assert( SCIPsetIsLT(set, row->vals[idx], 0.0) );
      row->lhs -= row->vals[idx] * SCIPvarGetLbGlobal(var);
   }

   --row->nnz;
   row->vals[idx] = 0.0;
   row->inds[pos] = row->inds[row->nnz];
}

/* weaken generalized resolution row by setting variables to their global bounds */
static
SCIP_RETCODE weakenConflictRow(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_BDCHGIDX*        currbdchgidx,       /**< current bound change index */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds             /**< dense array of indices of fixed variables */
   )
{
   int i;
   int nvarsweakened;

   assert(row != NULL);
   assert(set != NULL);
   assert(vars != NULL);

   nvarsweakened = 0;

   for( i = row->nnz - 1; i >= 0; --i )
   {
      SCIP_VAR* vartoweaken;
      int idx;

      idx = row->inds[i];
      vartoweaken = vars[idx];

      if( row->vals[idx] > 0.0 )
      {
         SCIP_Real ub;

         ub = SCIPgetVarUbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);

         if( SCIPsetIsEQ(set, ub, SCIPvarGetUbGlobal(vartoweaken)) && (fixinds == NULL || fixinds[idx] == 0) )
         {
            weakenVarConflictRow(row, set, vartoweaken, i);
            ++nvarsweakened;

         }
      }
         else
         {
            SCIP_Real lb;

            lb = SCIPgetVarLbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);

            if( SCIPsetIsEQ(set, lb, SCIPvarGetLbGlobal(vartoweaken)) && (fixinds == NULL || fixinds[idx] == 0) )
            {
               weakenVarConflictRow(row, set, vartoweaken, i);
               ++nvarsweakened;
            }
         }
   }

   SCIPdebugMessage("weakened %d variables in the conflict row \n", nvarsweakened);

   return SCIP_OKAY;
}

/* weaken all continuous variables in a generalized resolution row */
static
SCIP_RETCODE weakenContinuousVarsConflictRow(
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   int                   residx              /**< index of variable we are resolving on */
   )
{
   int i;
   int nvarsweakened;

   assert(row != NULL);
   assert(set != NULL);
   assert(vars != NULL);

   nvarsweakened = 0;

   for( i = row->nnz - 1; i >= 0; --i )
   {
      SCIP_VAR* vartoweaken;
      int idx;

      idx = row->inds[i];
      vartoweaken = vars[idx];

      /* avoid weakening the variable we are resolving on */
      if ( idx == residx )
         continue;
      else if( row->vals[idx] > 0.0 && !SCIPvarIsIntegral(vartoweaken) )
      {
         weakenVarConflictRow(row, set, vartoweaken, i);
         ++nvarsweakened;
      }
      else if ( row->vals[idx] < 0.0 && !SCIPvarIsIntegral(vartoweaken) )
      {
         weakenVarConflictRow(row, set, vartoweaken, i);
         ++nvarsweakened;
      }
   }

   SCIPdebugMessage("weakened %d continuous variables in the row \n", nvarsweakened);

   return SCIP_OKAY;
}


/** returns the quotient of the largest and smallest value in a semi-sparse array */
static
SCIP_Real getQuotLargestSmallestCoef(
   SCIP_SET*             set,                /**< global SCIP settings */
   int*                  inds,               /**< array of indices */
   SCIP_Real*            vals,               /**< dense array of values */
   int                   nnz                 /**< number of nonzeros */
   )
   {
      int i;
      SCIP_Real minval;
      SCIP_Real maxval;

      assert( vals != NULL);

      if ( nnz == 0 )
         return 0.0;

      minval = SCIPsetInfinity(set);
      maxval = -SCIPsetInfinity(set);

      for ( i = 0; i < nnz; i++)
      {
         int idx;
         idx = inds[i];
         minval = MIN(minval, vals[idx]);
         maxval = MAX(maxval, vals[idx]);
      }
      return REALABS(maxval / minval);
   }

/** calculates the slack (maxact - rhs) for a generalized resolution row given a set of bounds and coefficients */
static
SCIP_Real getSlackConflictRow(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     row,                /**< generalized resolution row */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< current bound change */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds             /**< dense array of indices of fixed variables */
   )
{
   SCIP_BDCHGIDX * currbdchgidx;
   SCIP_Real QUAD(slack);
   int i;

   assert(vars != NULL);

#ifdef SCIP_MORE_DEBUG
   SCIPsetDebugMsgPrint(set, "Calculating slack for row at depth %d position %d \n", SCIPbdchginfoGetDepth(currbdchginfo), SCIPbdchginfoGetPos(currbdchginfo));
#endif
   QUAD_ASSIGN(slack, 0.0);
   currbdchgidx = SCIPbdchginfoGetIdx(currbdchginfo);
   for( i = 0; i < row->nnz; i++ )
   {
      SCIP_Real coef;
      SCIP_Real bound;
      SCIP_Real QUAD(delta);
      int v;
      v = row->inds[i];

      assert(SCIPvarGetProbindex(vars[v]) == v);

      coef = row->vals[v];
      bound = 0.0;

      /* get the latest bound change before currbdchgidx */
      if( coef > 0.0 )
      {
         if ( fixinds != NULL && fixinds[v] == 1 ) /* if the variable is fixed */
         {
            bound = fixbounds[v];
         }
         else
         {
            bound = SCIPgetVarUbAtIndex(set->scip, vars[v], currbdchgidx, TRUE);
         }
         SCIPquadprecProdDD(delta, coef, bound);
      }
      else
      {
         if (fixinds != NULL && fixinds[v] == -1) /* if the variable is fixed */
         {
            bound = fixbounds[v];
         }
         else
         {
            bound = SCIPgetVarLbAtIndex(set->scip, vars[v], currbdchgidx, TRUE);
         }
         SCIPquadprecProdDD(delta, coef, bound);
      }
      SCIPquadprecSumQQ(slack, slack, delta);
#ifdef SCIP_MORE_DEBUG
      SCIPsetDebugMsgPrint(set, "var: %s, coef: %f, bound: %f global bounds:[%.15g,%.15g], current bounds:[%.15g,%.15g] \n",
                           SCIPvarGetName(vars[v]), coef, bound, SCIPvarGetLbGlobal(vars[v]), SCIPvarGetUbGlobal(vars[v]),
                           SCIPgetVarLbAtIndex(set->scip, vars[v], currbdchgidx, TRUE), SCIPgetVarUbAtIndex(set->scip, vars[v],
                           currbdchgidx, TRUE));
      SCIPsetDebugMsgPrint(set, "slack: %f \n",QUAD_TO_DBL(slack) );
#endif
   }
   SCIPquadprecSumQD(slack, slack, -row->lhs);
#ifdef SCIP_MORE_DEBUG
   SCIPsetDebugMsgPrint(set, "Row slack: %f \n",QUAD_TO_DBL(slack) );
#endif
   return QUAD_TO_DBL(slack);
}


/** for every variable in the row, except the inferred variable, add bound changes */
static
SCIP_RETCODE updateBdchgQueue(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row */
   SCIP_BDCHGIDX*        inferbdchgidx       /**< bound change index (time stamp of bound change), or NULL for current time */
   )
{
   int i;

   assert(vars != NULL);

   /* scan through the row and add bound changes that make the constraint infeasible */
   for( i = 0; i < conflictrow->nnz; i++ )
   {
      SCIP_Real coef;
      int v;
      v = conflictrow->inds[i];

      assert(SCIPvarGetProbindex(vars[v]) == v);

      coef = conflictrow->vals[v];

      if( coef > 0.0 )
      {
         /* refactortodo this may not work for multiple bdchgs ie problems with continuous and general integers */
         SCIP_Real bnd;
         if ( SCIPvarGetNBdchgInfosUb(vars[v]) > 0 )
         {
            bnd = SCIPgetVarUbAtIndex(set->scip, vars[v], inferbdchgidx, FALSE);
            if ( SCIPsetIsLT(set, bnd, SCIPvarGetUbGlobal(vars[v])) )
            {
               SCIP_CALL( SCIPaddConflictUb(set->scip, vars[v], inferbdchgidx) );
            }
         }
      }
      else
      {
         SCIP_Real bnd;
         if ( SCIPvarGetNBdchgInfosLb(vars[v]) > 0 )
         {
            bnd = SCIPgetVarLbAtIndex(set->scip, vars[v], inferbdchgidx, FALSE);
            if ( SCIPsetIsGT(set, bnd, SCIPvarGetLbGlobal(vars[v])) )
            {
               SCIP_CALL( SCIPaddConflictLb(set->scip, vars[v], inferbdchgidx) );
            }
         }
      }
   }
   return SCIP_OKAY;
}

/** add all slack reducing continuous bound changes to the continuous bound change queue */
static
SCIP_RETCODE slackReducingContinuousBdchgQueue(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     row,                /**< conflict row */
   SCIP_BDCHGIDX*        inferbdchgidx       /**< bound change index of latest continuous bound change */
   )
{
   SCIP_PQUEUE* continuousbdchgqueue;
   int i;

   assert(vars != NULL);

   continuousbdchgqueue = conflict->continuousbdchgqueue;

   /* make sure that the continuous bound change queue is empty */
   if (SCIPpqueueNElems(conflict->continuousbdchgqueue) != 0)
      SCIPpqueueClear(conflict->continuousbdchgqueue);

   /** For a row of the form ax >= b, we can add all continuous bound changes before inferbdchgidx that reduce the slack
    *  for each continuous variable x_k with coefficient a_k:
    *  - a_k > 0: then add bound changes (if not already present). Uses SCIPvarGetUbchgInfo() to get the latest
    *    bound change used in the slack of row.
    *  - a_k < 0: then add bound changes (if not already present). Uses SCIPvarGetLbchgInfo() to get the latest
    *    bound change used in the slack of row.
   */
   for( i = 0; i < row->nnz; i++ )
   {
      SCIP_Real coef;
      int v;

      v = row->inds[i];

      assert(SCIPvarGetProbindex(vars[v]) == v);

      if ( SCIPvarIsIntegral(vars[v]) )
         continue;

      coef = row->vals[v];

      if( coef > 0.0 )
      {
         SCIP_BDCHGINFO* bdchginfo = SCIPvarGetUbchgInfo(vars[v], inferbdchgidx, TRUE);
         if (bdchginfo != NULL)
            SCIP_CALL( SCIPpqueueInsert(continuousbdchgqueue, (void*)bdchginfo) );
      }
      else
      {
         SCIP_BDCHGINFO* bdchginfo = SCIPvarGetLbchgInfo(vars[v], inferbdchgidx, TRUE);
         if (bdchginfo != NULL)
            SCIP_CALL( SCIPpqueueInsert(continuousbdchgqueue, (void*)bdchginfo) );
      }
   }
   return SCIP_OKAY;
}

/* check the conflict with debugging solution */
static
SCIP_RETCODE checkConflictDebugSol(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_CONFLICTROW*     conflictrow         /**< conflict row to add to the tree */
   )
{

   SCIP_VAR** consvars;
   SCIP_CONS* cons;
   char consname[SCIP_MAXSTRLEN];

   SCIP_Real* vals;
   SCIP_Real lhs;
   int i;

   assert(vars != NULL);

   SCIP_CALL( SCIPallocBufferArray(set->scip, &consvars, conflictrow->nnz) );
   SCIP_CALL( SCIPallocBufferArray(set->scip, &vals, conflictrow->nnz) );

   lhs = conflictrow->lhs;

   for( i = 0; i < conflictrow->nnz; ++i )
   {
      int idx;
      idx = conflictrow->inds[i];
      assert(conflictrow->vals[idx]);
      consvars[i] = vars[idx];
      vals[i] = conflictrow->vals[idx];
   }

   /* create a constraint out of the conflict set */
   (void) SCIPsnprintf(consname, SCIP_MAXSTRLEN, "confres_%" SCIP_LONGINT_FORMAT, conflict->nresconfconss);
   SCIP_CALL( SCIPcreateConsLinear(set->scip, &cons, consname, conflictrow->nnz, consvars, vals,
              lhs, SCIPsetInfinity(set), FALSE, set->conf_separesolution, FALSE, FALSE, TRUE, (SCIPnodeGetDepth(tree->path[conflictrow->validdepth]) > 0 ),
              FALSE, set->conf_dynamic, set->conf_removable, FALSE) );

   /* check if the constraint is valid for the debug solution before upgrade */
   SCIP_CALL( SCIPdebugCheckAnyConss(set->scip, &cons, 1) );

   SCIP_CALL( SCIPreleaseCons(set->scip, &cons) );

   /* free temporary memory */
   SCIPfreeBufferArray(set->scip, &consvars);
   SCIPfreeBufferArray(set->scip, &vals);

   return SCIP_OKAY;
}/*lint !e715*/

/* refactortodo: Check the updates */
/** increases the conflict score of the variable in the given direction */
static
SCIP_RETCODE incVSIDS(
   SCIP_VAR*             var,                /**< problem variable */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_BOUNDTYPE        boundtype,          /**< type of bound for which the score should be increased */
   SCIP_Real             value,              /**< value of the bound */
   SCIP_Real             weight              /**< weight of this VSIDS updates */
   )
{
   SCIP_BRANCHDIR branchdir;

   assert(var != NULL);
   assert(stat != NULL);

   /* weight the VSIDS by the given weight */
   weight *= stat->vsidsweight;

   if( SCIPsetIsZero(set, weight) )
      return SCIP_OKAY;

   branchdir = (boundtype == SCIP_BOUNDTYPE_LOWER ? SCIP_BRANCHDIR_UPWARDS : SCIP_BRANCHDIR_DOWNWARDS); /*lint !e641*/
   SCIP_CALL( SCIPvarIncVSIDS(var, blkmem, set, stat, branchdir, value, weight) );
   SCIPhistoryIncVSIDS(stat->glbhistory, branchdir,  weight);
   SCIPhistoryIncVSIDS(stat->glbhistorycrun, branchdir,  weight);

   return SCIP_OKAY;
}

/** update conflict statistics */
static
SCIP_RETCODE updateStatistics(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_VAR**            vars,               /**< array of variables */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row to add to the tree */
   int                   insertdepth         /**< depth level at which the conflict set should be added */
   )
{
   if( insertdepth > 0 )
   {
      conflict->nappliedlocconss++;
      conflict->nappliedlocliterals += conflictrow->nnz;
   }
   else
   {
      int i;
      int conflictlength;
      conflictlength = conflictrow->nnz;

      assert(vars != NULL);

      for( i = 0; i < conflictlength; i++ )
      {
         int idx;
         SCIP_VAR* var;
         SCIP_BRANCHDIR branchdir;
         SCIP_BOUNDTYPE boundtype;
         SCIP_Real bound;

         assert(stat != NULL);

         idx = conflictrow->inds[i];
         var = vars[idx];
         /* todo atm this does not work for general MIP */
         /* to make this work we need the relaxed bound for the integer and
          * continuous variables. We can get them by giving to this function the
          * UIP bdchgidx as parameter and checking if there exist bound changes
          * for variables before that bound change that lead to the
          * infeasibility of the conflict constraint. Alternative: Can we get
          * these values directly these values from the resbdchgqueue and the
          * current bdchg and all previous ones that have been fixed? */
         assert(!SCIPsetIsZero(set, conflictrow->vals[idx]));
         boundtype = conflictrow->vals[idx] > 0 ? SCIP_BOUNDTYPE_UPPER : SCIP_BOUNDTYPE_LOWER;
         bound = conflictrow->vals[idx] > 0 ? SCIPvarGetLbLocal(var) : SCIPvarGetUbLocal(var);

         branchdir = (boundtype == SCIP_BOUNDTYPE_LOWER ? SCIP_BRANCHDIR_UPWARDS : SCIP_BRANCHDIR_DOWNWARDS); /*lint !e641*/

         SCIP_CALL( SCIPvarIncNActiveConflicts(var, blkmem, set, stat,  branchdir, bound, (SCIP_Real)conflictlength) );
         SCIPhistoryIncNActiveConflicts(stat->glbhistory, branchdir, (SCIP_Real)conflictlength);
         SCIPhistoryIncNActiveConflicts(stat->glbhistorycrun, branchdir, (SCIP_Real)conflictlength);

         /* each variable which is part of the conflict gets an increase in the VSIDS */
         SCIP_CALL( incVSIDS(var, blkmem, set, stat, boundtype, bound, set->conf_conflictweight) );
      }
      conflict->nappliedglbconss++;
      conflict->nappliedglbliterals += conflictrow->nnz;
   }

   return SCIP_OKAY;
}

/** creates a conflict constraint and tries to add it to the storage */
static
SCIP_RETCODE createAndAddConflictCon(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row to add to the tree */
   int                   insertdepth,        /**< depth level at which the conflict set should be added */
   SCIP_Bool*            success             /**< pointer to store whether the addition was successful */
   )
{

   SCIP_VAR** consvars;
   SCIP_CONS* cons;
   SCIP_CONS* upgdcons;

   char consname[SCIP_MAXSTRLEN];

   SCIP_Real* vals;
   SCIP_Real lhs;
   int i;

   assert(vars != NULL);
   assert(conflictrow->nnz > 0);

   SCIP_CALL( SCIPallocBufferArray(set->scip, &consvars, conflictrow->nnz) );
   SCIP_CALL( SCIPallocBufferArray(set->scip, &vals, conflictrow->nnz) );

   lhs = conflictrow->lhs;

   for( i = 0; i < conflictrow->nnz; ++i )
   {
      int idx;
      idx = conflictrow->inds[i];
      assert(conflictrow->vals[idx]);
      consvars[i] = vars[idx];
      vals[i] = conflictrow->vals[idx];
   }

   /* create a constraint out of the conflict set */
   (void) SCIPsnprintf(consname, SCIP_MAXSTRLEN, "confres_%" SCIP_LONGINT_FORMAT, conflict->nresconfconss);
   SCIP_CALL( SCIPcreateConsLinear(set->scip, &cons, consname, conflictrow->nnz, consvars, vals,
              lhs, SCIPsetInfinity(set), FALSE, set->conf_separesolution, FALSE, FALSE, TRUE, (SCIPnodeGetDepth(tree->path[conflictrow->validdepth]) > 0 ),
              FALSE, set->conf_dynamic, set->conf_removable, FALSE) );

   /* check if the constraint is valid for the debug solution */
   SCIP_CALL( SCIPdebugCheckAnyConss(set->scip, &cons, 1) );

   /* try to automatically convert a linear constraint into a more specific and more specialized constraint */
   if (set->conf_upgrade)
   {
      SCIP_CALL( SCIPupgradeConsLinear(set->scip, cons, &upgdcons) );
      if( upgdcons != NULL )
      {
         SCIP_CALL( SCIPreleaseCons(set->scip, &cons) );
         cons = upgdcons;
      }
   }
   /* check if the constraint is valid for the debug solution */
   SCIP_CALL( SCIPdebugCheckAnyConss(set->scip, &cons, 1) );


   /* update statistics */
   SCIP_CALL( updateStatistics(conflict, vars, blkmem, set, stat, conflictrow, conflictrow->validdepth) );

   /* add conflict to SCIP */
   cons->resconflict = TRUE;
   /* todo add different conflict types for the different cases */
   SCIP_CALL( SCIPaddConflict(set->scip, tree->path[insertdepth], cons, tree->path[conflictrow->validdepth], SCIP_CONFTYPE_RESOLUTION, conflict->conflictrow->usescutoffbound) );
   *success = TRUE;
   /* free temporary memory */
   SCIPfreeBufferArray(set->scip, &consvars);
   SCIPfreeBufferArray(set->scip, &vals);

   return SCIP_OKAY;
}/*lint !e715*/

/** postprocess conflict constraint */
static
SCIP_RETCODE postprocessConflictCon(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row to add to the tree */
   int                   insertdepth,        /**< depth level at which the conflict set should be added */
   SCIP_Bool*            infeasible          /**< pointer to store whether the addition was successful */
   )
{

   SCIP_VAR** consvars;
   SCIP_CONS* cons;

   char consname[SCIP_MAXSTRLEN];

   SCIP_Real* vals;
   SCIP_Real lhs;
   int i;
   int arraysize;

   assert(vars != NULL);

   arraysize = conflictrow->nnz;
   SCIP_CALL( SCIPallocBufferArray(set->scip, &consvars, arraysize) );
   SCIP_CALL( SCIPallocBufferArray(set->scip, &vals, arraysize) );

   lhs = conflictrow->lhs;

   for( i = 0; i < conflictrow->nnz; ++i )
   {
      int idx;
      idx = conflictrow->inds[i];
      assert(conflictrow->vals[idx]);
      consvars[i] = vars[idx];
      vals[i] = conflictrow->vals[idx];
   }

   /* create a constraint out of the conflict set */
   (void) SCIPsnprintf(consname, SCIP_MAXSTRLEN, "confres_%" SCIP_LONGINT_FORMAT, conflict->nresconfconss);
   SCIP_CALL( SCIPcreateConsLinear(set->scip, &cons, consname, conflictrow->nnz, consvars, vals,
              lhs, SCIPsetInfinity(set), FALSE, set->conf_separesolution, FALSE, FALSE, TRUE, (SCIPnodeGetDepth(tree->path[conflictrow->validdepth]) > 0 ),
              FALSE, set->conf_dynamic, set->conf_removable, FALSE) );

   int nchgcoefs;
   int nchgsides;
   nchgcoefs = 0;
   nchgsides = 0;
   *infeasible = FALSE;

   SCIP_CALL(SCIPsimplifyConsLinear(set->scip, cons, &nchgcoefs, &nchgsides, infeasible));
   if((nchgcoefs != 0 || nchgsides != 0) && !(*infeasible))
   {
      SCIP_ROW* postprocessedrow;
      SCIP_COL** cols;
      SCIP_Real* vals;

      postprocessedrow = SCIPconsCreateRow(set->scip, cons);

      assert(SCIProwGetNNonz(postprocessedrow) <= conflictrow->nnz);

      for(i = 0; i < conflictrow->nnz; ++i)
         conflictrow->vals[conflictrow->inds[i]] = 0.0;

      conflictrow->nnz = SCIProwGetNNonz(postprocessedrow);
      assert(conflictrow->nnz > 0);

      vals = SCIProwGetVals(postprocessedrow);
      cols = SCIProwGetCols(postprocessedrow);

      conflictrow->lhs = SCIProwGetLhs(postprocessedrow) - SCIProwGetConstant(postprocessedrow);

      SCIP_Bool swapside = FALSE;
      if(SCIPsetIsInfinity(set, -conflictrow->lhs))
      {
         conflictrow->lhs = -(SCIProwGetRhs(postprocessedrow) - SCIProwGetConstant(postprocessedrow));
         assert(!SCIPsetIsInfinity(set, conflictrow->lhs));
         swapside = TRUE;
      }
      for(i = 0; i < conflictrow->nnz; ++i)
      {
         conflictrow->inds[i] = SCIPvarGetProbindex(SCIPcolGetVar(cols[i]));
         if(swapside)
            conflictrow->vals[conflictrow->inds[i]] = -vals[i];
         else
            conflictrow->vals[conflictrow->inds[i]] = vals[i];
      }

      SCIPdebug(printConflictRow(conflictrow, set, vars, POSTPROCESSED_CONFLICT_ROWTYPE));
   }

   SCIP_CALL( SCIPreleaseCons(set->scip, &cons) );

   /* free temporary memory */
   SCIPfreeBufferArray(set->scip, &consvars);
   SCIPfreeBufferArray(set->scip, &vals);

   return SCIP_OKAY;
}/*lint !e715*/


/** create conflict constraints out of conflict rows and add them to the problem */
SCIP_RETCODE SCIPconflictAddConflictCon(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_PROB*            transprob,          /**< transformed problem */
   SCIP_PROB*            origprob,           /**< original problem */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row to add to the tree */
   SCIP_Bool*            success             /**< true if the conflict is added to the problem */
   )
{
   SCIP_VAR** vars;
   SCIP_Bool infeasible;
   int focusdepth;
   int maxsize;

   vars = SCIPprobGetVars(transprob);

   focusdepth = SCIPtreeGetFocusDepth(tree);

   assert(conflict != NULL);
   assert(set != NULL);
   assert(stat != NULL);
   assert(transprob != NULL);
   assert(tree != NULL);
   assert(conflictrow != NULL);
   assert(conflictrow->validdepth == 0);
   assert(vars != NULL);

   assert(focusdepth <= SCIPtreeGetCurrentDepth(tree));
   assert(SCIPtreeGetCurrentDepth(tree) == tree->pathlen-1);

   /* calculate the maximal size of each accepted conflict set */
   maxsize = (int) (set->conf_maxvarsfracres * transprob->nvars);

   if(set->conf_postprocessconflicts)
   {
      infeasible = FALSE;
      SCIP_CALL(postprocessConflictCon(conflict, blkmem, set, stat, vars, tree, reopt, lp, cliquetable, conflictrow, focusdepth, &infeasible));
      if(infeasible)
      {
         SCIPsetDebugMsgPrint(set, "postprocessing proves global infeasibility \n");
         /* increase the number of aborts since no conflict constraint is added */
         conflict->nknownaborts++;
         SCIP_CALL( SCIPnodeCutoff(tree->path[conflictrow->validdepth], set, stat, tree, transprob, origprob, reopt, lp, blkmem) );
         return SCIP_OKAY;

      }
   }
   /* refactortodo compute insert depth */

   SCIPsetDebugMsgPrint(set, "flushing %d conflict constraint at focus depth %d (id: %d, vd: %d, cd: %d, rd: %d, maxsize: %d)\n",
      1, focusdepth, conflictrow->insertdepth, conflictrow->validdepth, conflictrow->conflictdepth, conflictrow->repropdepth, maxsize);

   *success = FALSE;
   /* do not add long conflicts */
   if( conflictrow->nnz > maxsize )
   {
      conflict->nknownaborts++;
      conflict->nreslongconfs++;
      SCIPsetDebugMsgPrint(set, " \t -> conflict row is too long: %d > %d nnzs\n", conflictrow->nnz, maxsize);
   }
   /* if the conflict row is empty and the lhs positive, the node and its sub-
    * tree in the conflict row's valid depth can be cut off completely
    */
   else if( conflictrow->nnz == 0 && SCIPsetIsGT(set, conflictrow->lhs, 0.0))
   {
      SCIPsetDebugMsgPrint(set, " \t -> empty conflict row with lhs %f in depth %d cuts off sub tree at depth %d\n",
         conflictrow->lhs, focusdepth, conflictrow->validdepth);
      *success = TRUE;
      SCIP_CALL( SCIPnodeCutoff(tree->path[conflictrow->validdepth], set, stat, tree, transprob, origprob, reopt, lp, blkmem) );
   }
   /* in case the conflict set contains only one bound change which is globally valid we apply that bound change
    * directly (except if we are in strong branching or diving - in this case a bound change would yield an unflushed LP
    * and is not handled when restoring the information)
    *
    * @note A bound change can only be applied if it is are related to the active node or if is a global bound
    *       change. Bound changes which are related to any other node cannot be handled at point due to the internal
    *       data structure
    */
   else if( !hasRelaxationOnlyVar(set, vars, conflictrow) && conflictrow->nnz == 1 && conflictrow->insertdepth == 0 && !lp->strongbranching && !lp->diving )
   {
      SCIP_VAR* var;
      SCIP_Real bound;
      SCIP_BOUNDTYPE boundtype;
      int idx;

      idx = conflictrow->inds[0];
      var = vars[idx];
      assert(!SCIPsetIsZero(set, conflictrow->vals[idx]));
      assert(var != NULL);

      boundtype = conflictrow->vals[idx] > 0.0 ? SCIP_BOUNDTYPE_LOWER : SCIP_BOUNDTYPE_UPPER;

      /** Since the conflictrow is in the form a*x >= b:
       *  For integer variables:
       *    coef > 0: The lower bound change is equal to ceil(lhs / coef)
       *    coef < 0: The upper bound change is equal to floor(lhs / coef)
       *  For continuous variables we do not round the bound change
       */
      bound = conflictrow->lhs / conflictrow->vals[idx];
      if( SCIPvarIsIntegral(var) )
         bound = conflictrow->vals[idx] > 0.0 ? SCIPsetCeil(set, bound) : SCIPsetFloor(set, bound);

      /* refactortodo: rethink the logic and add asserts. Also find a better way
       * to inform the statistics that a constraint is added "inderectly", also
       * update number of global bound changes from conflict analysis */
      if(boundtype == SCIP_BOUNDTYPE_LOWER && SCIPsetIsFeasGT(set, bound, SCIPvarGetUbGlobal(var)))
      {
         SCIPsetDebugMsgPrint(set, " \t -> Bound %s >= %f contradicts with the global upper bound %s <= %f\n",
            SCIPvarGetName(var), bound, SCIPvarGetName(var), SCIPvarGetUbGlobal(var));
         SCIP_CALL( SCIPnodeCutoff(tree->path[conflictrow->validdepth], set, stat, tree, transprob, origprob, reopt, lp, blkmem) );
      }
      else if(boundtype == SCIP_BOUNDTYPE_UPPER && SCIPsetIsLT(set, bound, SCIPvarGetLbGlobal(var)))
      {
         SCIPsetDebugMsgPrint(set, " \t -> Bound %s <= %f contradicts with the global lower bound %s >= %f\n",
            SCIPvarGetName(var), bound, SCIPvarGetName(var), SCIPvarGetUbGlobal(var));
         SCIP_CALL( SCIPnodeCutoff(tree->path[conflictrow->validdepth], set, stat, tree, transprob, origprob, reopt, lp, blkmem) );
      }
      else
      {
         SCIPsetDebugMsg(set, " -> apply global bound change: <%s> %s %g\n",
            SCIPvarGetName(var), boundtype == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=", bound);
        SCIP_CALL( SCIPnodeAddBoundchg(tree->path[conflictrow->validdepth], blkmem, set, stat, transprob, origprob, tree,
               reopt, lp, branchcand, eventqueue, cliquetable, var, bound, boundtype, FALSE) );
      }

      *success = TRUE;
      SCIP_CALL( updateStatistics(conflict, vars, blkmem, set, stat, conflictrow, conflictrow->validdepth) );
   }
   /* generate the linear constraint */
   else if( !hasRelaxationOnlyVar(set, vars, conflictrow) )
   {
      /* @todo use the right insert depth and not valid depth */
      SCIP_CALL( createAndAddConflictCon(conflict, blkmem, set, stat, vars, \
                     tree, reopt, lp, cliquetable, conflictrow, conflictrow->insertdepth, success) );
      conflict->nappliedglbresconss++;
      SCIPsetDebugMsgPrint(set, " \t -> conflict row added (cdpt:%d, fdpt:%d, insert:%d, valid:%d, conf: %d, reprop: %d, len:%d):\n",
                     SCIPtreeGetCurrentDepth(tree), SCIPtreeGetFocusDepth(tree),
                     conflictrow->insertdepth, conflictrow->validdepth, conflictrow->conflictdepth,
                     conflictrow->repropdepth, conflictrow->nnz);
   }
   else
   {
      conflict->nknownaborts++;
      SCIPsetDebugMsgPrint(set, " \t -> conflict row has relaxation only variable \n");
      return SCIP_OKAY;
   }

   return SCIP_OKAY;
}/*lint !e715*/

/** adds given data as row to the generalized resolution row */
static
SCIP_RETCODE conflictRowAddSemiSparseData(
   SCIP_CONFLICTROW*     resolutionrow,      /**< generalized resolution row */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_Real*            vals,               /**< variable coefficients */
   int*                  inds,               /**< variable array */
   int                   nnz,                /**< size of variable and coefficient array */
   SCIP_Real             lhs,                /**< left-hand side of conflict row */
   SCIP_Bool             reverse             /**< reverse coefficients */

   )
{
   int i;
   int idx;

   assert(resolutionrow != NULL);
   assert(resolutionrow->vals != NULL);
   assert(blkmem != NULL);

   if( resolutionrow->size == 0 )
   {
      assert(resolutionrow->inds == NULL);

      SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &resolutionrow->inds, nnz) );
      resolutionrow->size = nnz;
   }
   else
   {
      assert(resolutionrow->vals != NULL);
      assert(resolutionrow->inds != NULL);

      if( resolutionrow->size < nnz )
      {
         SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &resolutionrow->inds, resolutionrow->size, nnz) );
         resolutionrow->size = nnz;
      }
   }

   if ( reverse )
   {
      for( i = 0; i < nnz; i++ )
      {
         idx = inds[i];
         resolutionrow->vals[idx] = -vals[i];
         resolutionrow->inds[i] = inds[i];
      }
   }
   else
   {
      for( i = 0; i < nnz; i++ )
      {
         idx = inds[i];
         resolutionrow->vals[idx] = vals[i];
         resolutionrow->inds[i] = inds[i];
      }
   }

   resolutionrow->lhs = lhs;
   resolutionrow->nnz = nnz;

   return SCIP_OKAY;
}

/** compute scale for the reason constraint such that the resolving variable cancels */
static
SCIP_Real computeScaleReason(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   int                   residx              /**< index of variable to resolve */
   )
{
   SCIP_Real coefconf;
   SCIP_Real coefreas;
   SCIP_Real scale;

   coefconf = conflictrow->vals[residx];

   coefreas = reasonrow->vals[residx];

   assert(!SCIPsetIsZero(set, coefreas) && !SCIPsetIsZero(set, coefconf));
   assert(coefconf * coefreas < 0);

   scale = REALABS( coefconf / coefreas );

   return scale;

}

/* get a clause out of the current bound changes and the fixed bounds */
static
SCIP_RETCODE getConflictClause(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   SCIP_Bool*            success,            /**< pointer to store whether we could find an  initial conflict */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< dense array of indices of fixed variables */
   int                   nvars,              /**< number of variables */
   SCIP_Bool             initial             /**< whether we are in the initialization conflict analysis */
)
{
   SCIPsetDebugMsgPrint(set, "Getting conflict clause: \n");

   *success = FALSE;

   if( !set->conf_clausegenres && !set->conf_clausefallback )
      return SCIP_OKAY;

   if( SCIPvarIsBinary(SCIPbdchginfoGetVar(currbdchginfo)) && SCIPsetGetStage(set) == SCIP_STAGE_SOLVING )
   {
      SCIP_CONFLICTROW* conflictrow;
      int* includeinconflict;
      SCIP_Bool isbinary;
      SCIP_Real lhs;
      int nfixinds;
      int pos;
      int idx;

      conflictrow = conflict->conflictrow;
      isbinary = TRUE;
      lhs = 1.0;
      nfixinds = 0;

      SCIP_CALL( SCIPsetAllocBufferArray(set, &includeinconflict, SCIPpqueueNElems(conflict->resbdchgqueue) ) );
      /** given the set of bound changes that renders infeasibility we can create a no-good cut
       *  as initial conflict. E.g. if x = 1, y = 1, z = 0 leads to infeasibility,
       *  then the initial conflict constraint is (1 - x) + (1 - y) + z >= 1
       */
      for(int i = 0; i < SCIPpqueueNElems(conflict->resbdchgqueue); i++)
      {
         SCIP_BDCHGINFO* bdchginfo;
         bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->resbdchgqueue)[i]);
         if(initial || bdchginfoUsedForConflict(conflict, bdchginfo, FALSE))
         {
            includeinconflict[i] = 1;
            if( !SCIPvarIsBinary(SCIPbdchginfoGetVar(bdchginfo)))
            {
               isbinary = FALSE;
               break;
            }
            if (SCIPbdchginfoGetNewbound(bdchginfo) >= 0.5)
               lhs--;
         }
         else
            includeinconflict[i] = 0;
      }

      if (isbinary && fixinds != NULL)
      {
         for(int i = 0; i < nvars; i++)
         {
            if( fixinds[i] != 0 )
            {
               if( !SCIPvarIsBinary(vars[i]) )
               {
                  isbinary = FALSE;
                  break;
               }
               if (fixbounds[i] >= 0.5)
                  lhs--;
               nfixinds++;
            }
         }
      }

      if( isbinary )
      {

         /* clear the row before creating a new row for the clause */
         conflictRowClear(blkmem, conflictrow, nvars);

         conflictrow->nnz = SCIPpqueueNElems(conflict->resbdchgqueue) + 1 + nfixinds;
         conflictrow->lhs = lhs;

         if( conflictrow->size == 0 )
         {
            assert(conflictrow->inds == NULL);

            SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &conflictrow->inds, conflictrow->nnz) );
            conflictrow->size = conflictrow->nnz;
         }

         else if( conflictrow->size < conflictrow->nnz )
         {
            SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &conflictrow->inds, conflictrow->size, conflictrow->nnz) );
            conflictrow->size = conflictrow->nnz;
         }

         idx = SCIPvarGetProbindex(SCIPbdchginfoGetVar(currbdchginfo));
         /* for the current bound change */
         if (SCIPbdchginfoGetNewbound(currbdchginfo) > 0.5)
         {
            conflictrow->vals[idx] = -1.0;
            conflictrow->lhs -= 1.0;
         }
         else
            conflictrow->vals[idx] = 1.0;
         conflictrow->inds[0] = SCIPvarGetProbindex(SCIPbdchginfoGetVar(currbdchginfo));
         pos = 1;

         if (fixinds != NULL)
         {
            for(int i = 0; i < nvars; i++)
            {
               if( fixinds[i] != 0 )
               {
                  conflictrow->vals[i] = fixbounds[i] > 0.5 ? -1.0 : 1.0;
                  conflictrow->inds[pos] = i;
                  pos++;
               }
            }
         }

         for( int i = 0; i < SCIPpqueueNElems(conflict->resbdchgqueue); i++ )
         {
            SCIP_BDCHGINFO* bdchginfo;
            bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->resbdchgqueue)[i]);
            if( includeinconflict[i] == 1 )
            {
               idx = SCIPvarGetProbindex(SCIPbdchginfoGetVar(bdchginfo));
               conflictrow->vals[idx] = SCIPbdchginfoGetNewbound(bdchginfo) > 0.5 ? -1.0 : 1.0;
               conflictrow->inds[pos] = idx;
               pos++;
            }
            else
               conflictrow->nnz--;
         }
         *success = TRUE;
      }
      SCIPsetFreeBufferArray(set, &includeinconflict);
   }
   if( !(*success) )
      SCIPsetDebugMsgPrint(set, " \t -> cannot create clause because of non-binary variable \n");
   return SCIP_OKAY;
}

/** tries to resolve given bound change */
static
SCIP_RETCODE reasonBoundChanges(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BDCHGINFO*       bdchginfo,          /**< bound change to resolve */
   SCIP_Real             relaxedbd,          /**< the relaxed bound */
   int                   validdepth,         /**< minimal depth level at which the conflict is valid */
   SCIP_Bool*            resolved            /**< pointer to store whether the bound change was resolved */
   )
{
   SCIP_VAR* actvar;
   SCIP_CONS* infercons;
   SCIP_PROP* inferprop;
   SCIP_RESULT result;

#ifndef NDEBUG

   /* store the current size of the conflict queues */
   assert(conflict != NULL);
#else
   assert(conflict != NULL);
#endif

   assert(!SCIPbdchginfoIsRedundant(bdchginfo));

   *resolved = FALSE;

   actvar = SCIPbdchginfoGetVar(bdchginfo);
   assert(actvar != NULL);
   assert(SCIPvarIsActive(actvar));

   /* check, if the bound change can and should be resolved:
    *  - the reason must be either a global constraint
    *  - or a propagator
    */
   switch( SCIPbdchginfoGetChgtype(bdchginfo) )
   {
   case SCIP_BOUNDCHGTYPE_CONSINFER:
      infercons = SCIPbdchginfoGetInferCons(bdchginfo);
      assert(infercons != NULL);

      if( SCIPconsIsGlobal(infercons) || SCIPconsGetValidDepth(infercons) <= validdepth )
      {
         SCIP_VAR* infervar;
         int inferinfo;
         SCIP_BOUNDTYPE inferboundtype;
         SCIP_BDCHGIDX* bdchgidx;

         /* resolve bound change by asking the constraint that infered the bound to put all bounds that were
          * the reasons for the conflicting bound change on the priority queue
          */
         infervar = SCIPbdchginfoGetInferVar(bdchginfo);
         inferinfo = SCIPbdchginfoGetInferInfo(bdchginfo);
         inferboundtype = SCIPbdchginfoGetInferBoundtype(bdchginfo);
         bdchgidx = SCIPbdchginfoGetIdx(bdchginfo);
         assert(infervar != NULL);

         SCIPsetDebugMsgPrint(set, " \t -> getting reason for <%s> %s %g(%g) [status:%d, type:%d, depth:%d, pos:%d]: <%s> %s %g [cons:<%s>(%s), info:%d]\n",
            SCIPvarGetName(actvar),
            SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPbdchginfoGetNewbound(bdchginfo), relaxedbd,
            SCIPvarGetStatus(actvar), SCIPvarGetType(actvar),
            SCIPbdchginfoGetDepth(bdchginfo), SCIPbdchginfoGetPos(bdchginfo),
            SCIPvarGetName(infervar),
            inferboundtype == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPgetVarBdAtIndex(set->scip, infervar, inferboundtype, bdchgidx, TRUE),
            SCIPconsGetName(infercons),
            SCIPconsIsGlobal(infercons) ? "global" : "local",
            inferinfo);

         /* in case the inference variables is not an active variables, we need to transform the relaxed bound */
         if( actvar != infervar )
         {
            SCIP_VAR* var;
            SCIP_Real scalar;
            SCIP_Real constant;

            assert(SCIPvarGetStatus(infervar) == SCIP_VARSTATUS_AGGREGATED
               || SCIPvarGetStatus(infervar) == SCIP_VARSTATUS_NEGATED
               || (SCIPvarGetStatus(infervar) == SCIP_VARSTATUS_MULTAGGR && SCIPvarGetMultaggrNVars(infervar) == 1));

            scalar = 1.0;
            constant = 0.0;

            var = infervar;

            /* transform given varibale to active varibale */
            SCIP_CALL( SCIPvarGetProbvarSum(&var, set, &scalar, &constant) );
            assert(var == actvar);

            relaxedbd *= scalar;
            relaxedbd += constant;
         }

         conflict->reasonclauseres = TRUE;
         SCIP_CALL( SCIPconsResolvePropagation(infercons, set, infervar, inferinfo, inferboundtype, bdchgidx, relaxedbd, &result) );
         *resolved = (result == SCIP_SUCCESS);
         conflict->reasonclauseres = FALSE;

      }
      break;

   case SCIP_BOUNDCHGTYPE_PROPINFER:
      inferprop = SCIPbdchginfoGetInferProp(bdchginfo);
      if( inferprop != NULL )
      {
         SCIP_VAR* infervar;
         int inferinfo;
         SCIP_BOUNDTYPE inferboundtype;
         SCIP_BDCHGIDX* bdchgidx;

         /* resolve bound change by asking the propagator that infered the bound to put all bounds that were
          * the reasons for the conflicting bound change on the priority queue
          */
         infervar = SCIPbdchginfoGetInferVar(bdchginfo);
         inferinfo = SCIPbdchginfoGetInferInfo(bdchginfo);
         inferboundtype = SCIPbdchginfoGetInferBoundtype(bdchginfo);
         bdchgidx = SCIPbdchginfoGetIdx(bdchginfo);
         assert(infervar != NULL);

         SCIPsetDebugMsgPrint(set, " \t -> getting reason for <%s> %s %g(%g) [status:%d, depth:%d, pos:%d]: <%s> %s %g [prop:<%s>, info:%d]\n",
            SCIPvarGetName(actvar),
            SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPbdchginfoGetNewbound(bdchginfo), relaxedbd,
            SCIPvarGetStatus(actvar), SCIPbdchginfoGetDepth(bdchginfo), SCIPbdchginfoGetPos(bdchginfo),
            SCIPvarGetName(infervar),
            inferboundtype == SCIP_BOUNDTYPE_LOWER ? ">=" : "<=",
            SCIPgetVarBdAtIndex(set->scip, infervar, inferboundtype, bdchgidx, TRUE),
            SCIPpropGetName(inferprop), inferinfo);

         conflict->reasonclauseres = TRUE;
         SCIP_CALL( SCIPpropResolvePropagation(inferprop, set, infervar, inferinfo, inferboundtype, bdchgidx, relaxedbd, &result) );
         *resolved = (result == SCIP_SUCCESS);
         conflict->reasonclauseres = FALSE;
      }
      break;

   case SCIP_BOUNDCHGTYPE_BRANCHING:
      assert(!(*resolved));
      break;

   default:
      SCIPerrorMessage(" \t -> invalid bound change type <%d>\n", SCIPbdchginfoGetChgtype(bdchginfo));
      return SCIP_INVALIDDATA;
   }

   SCIPsetDebugMsgPrint(set, " \t -> resolving status: %u\n", *resolved);

#ifndef NDEBUG

   /* in case the bound change was not resolved, the separate conflict queue should have zero elements */
   assert((*resolved) || (SCIPpqueueNElems(conflict->separatebdchgqueue) == 0));
#endif

   return SCIP_OKAY;
}

/* get a conflict row from bound changes */
static
SCIP_RETCODE getReasonClause(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   SCIP_Real             relaxedbd,          /**< the relaxed bound */
   int                   validdepth,         /**< minimal depth level at which the conflict is valid */
   SCIP_Bool*            success             /**< pointer to store whether we could find a reason*/
)
{
   SCIPsetDebugMsgPrint(set, "Getting reason clause: \n");

   *success = FALSE;

   if( !set->conf_clausegenres && !set->conf_clausefallback )
      return SCIP_OKAY;

   /* if the current bound change is on a non-binary variable then we cannot find a linear reason */
   if( !SCIPvarIsBinary(SCIPbdchginfoGetVar(currbdchginfo)) || !reasonIsLinearizable(currbdchginfo))
   {
      SCIPsetDebugMsgPrint(set, " \t -> cannot create clause because of non-binary variable \n");
      return SCIP_OKAY;
   }
   /* make sure that the separate bound change queue is empty */
   if (SCIPpqueueNElems(conflict->separatebdchgqueue) != 0)
      SCIPpqueueClear(conflict->separatebdchgqueue);

   SCIP_CALL( reasonBoundChanges(conflict, set, currbdchginfo, relaxedbd, validdepth, success) );
   if ( !(*success) || SCIPpqueueNElems(conflict->separatebdchgqueue) == 0 )
   {
      *success = FALSE;
      SCIPsetDebugMsgPrint(set, " \t -> cannot create clause because of unresolvable bound change \n");
      return SCIP_OKAY;
   }
   else
   {
      SCIP_CONFLICTROW* reasonrow;
      SCIP_Bool isbinary;
      SCIP_Real lhs;

      reasonrow = conflict->reasonrow;
      *success = FALSE;
      isbinary = TRUE;
      lhs = 1.0;

      /** given the set of bound changes that leads to propagation of the current
       *  bound change, create a clause reason conflict row
       *  E.g. if x = 1, y = 1, leads to z = 0 then the reason
       *  constraint is (1 - x) + (1 - y) + (1 - z) >= 1
       */
      for(int i = 0; i < SCIPpqueueNElems(conflict->separatebdchgqueue); i++)
      {
         SCIP_BDCHGINFO* bdchginfo;
         bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->separatebdchgqueue)[i]);

         if( !SCIPvarIsBinary(SCIPbdchginfoGetVar(bdchginfo)) )
         {
            isbinary = FALSE;
            break;
         }
         if (SCIPbdchginfoGetNewbound(bdchginfo) == 1.0)
            lhs--;
      }

      if( isbinary )
      {
         for( int i = 0; i < reasonrow->nnz; i++ )
            reasonrow->vals[reasonrow->inds[i]] = 0.0;

         reasonrow->nnz = SCIPpqueueNElems(conflict->separatebdchgqueue) + 1;
         reasonrow->lhs = lhs;

         if( reasonrow->size == 0 )
         {
            assert(reasonrow->inds == NULL);

            SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &reasonrow->inds, reasonrow->nnz) );
            reasonrow->size = reasonrow->nnz;
         }

         else if( reasonrow->size < reasonrow->nnz )
         {
            SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &reasonrow->inds, reasonrow->size, reasonrow->nnz) );
            reasonrow->size = reasonrow->nnz;
         }
         /* add the variable we are resolving and update lhs */
         reasonrow->inds[0] = SCIPvarGetProbindex(SCIPbdchginfoGetVar(currbdchginfo));
         int idx = reasonrow->inds[0];
         reasonrow->vals[idx] = SCIPbdchginfoGetNewbound(currbdchginfo) > 0.5 ? 1.0 : -1.0;

         reasonrow->lhs += SCIPbdchginfoGetNewbound(currbdchginfo) > 0.5 ? 0.0 : -1.0;

         for( int i = 0; i < reasonrow->nnz - 1; i++ )
         {
            SCIP_BDCHGINFO* bdchginfo;
            bdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueElems(conflict->separatebdchgqueue)[i]);
            reasonrow->inds[i+1] = SCIPvarGetProbindex(SCIPbdchginfoGetVar(bdchginfo));
            int idx = reasonrow->inds[i+1];
            reasonrow->vals[idx] = SCIPbdchginfoGetNewbound(bdchginfo) > 0.5 ? -1.0 : 1.0;


         }
         *success = TRUE;
      }
   }
   if(!(*success))
   {
       SCIPsetDebugMsgPrint(set, " \t -> cannot create clause because of non-binary variable \n");
      return SCIP_OKAY;
   }

   return SCIP_OKAY;
}

/** compute the resolved conflict row resolvedrow = row1 + scale * row2 */
static
SCIP_RETCODE rescaleAndResolve(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_CONFLICTROW*     row1,               /**< conflict row */
   SCIP_CONFLICTROW*     row2,               /**< reason conflict row */
   SCIP_CONFLICTROW*     resolvedrow,        /**< resolved conflict row */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   int                   residx,             /**< index of variable to resolve */
   SCIP_Bool*            success             /**< apply resolution */
   )
{

   int i;
   SCIP_Real scale;
   SCIP_Real largestcoef;
   SCIP_Real smallestcoef;

   int newsize;
   int newnnz;

   SCIPsetDebugMsgPrint(set, "Nonzeros in first row: %d, slack: %f \n", row1->nnz, row1->slack);
   SCIPsetDebugMsgPrint(set, "Nonzeros in second row: %d, slack: %f \n", row2->nnz, row2->slack);

   *success = FALSE;

   scale = computeScaleReason(set, row1, row2, residx);

   /* refactortodo this should not be a fixed value */
   if ( SCIPsetIsGE(set, scale, 1e+10) )
   {
      if( !conflict->haslargecoef )
      {
         conflict->haslargecoef = TRUE;
         conflict->nreslargecoefs++;
      }
      return SCIP_OKAY;
   }

   SCIP_CALL( conflictRowReplace(resolvedrow, blkmem, row1) );

   newsize = resolvedrow->nnz + row2->nnz;
   if ( resolvedrow->size < newsize )
   {
      assert(resolvedrow->vals != NULL);
      assert(resolvedrow->inds != NULL);

      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &resolvedrow->inds, resolvedrow->size, newsize ) );
      resolvedrow->size = newsize;
   }

   /* add the reason conflict row to the resolved conflict row */
   linearCombRows(set, resolvedrow, row2, scale);

   newnnz = resolvedrow->nnz;

   largestcoef = -SCIPsetInfinity(set);
   smallestcoef = SCIPsetInfinity(set);
   /* remove coefficients that are almost zero (1e-09 tolerance), loop backwards */
   for( i = newnnz - 1; i >= 0 ; i-- )
   {
      int idx;
      idx = resolvedrow->inds[i];
      if (SCIPsetIsZero(set, resolvedrow->vals[idx] ))
      {
         conflictRowRemoveZeroVar(resolvedrow, set, i);
      }
      else
      {
         smallestcoef = MIN(smallestcoef, resolvedrow->vals[idx]);
         largestcoef = MAX(largestcoef, resolvedrow->vals[idx]);
      }
   }
   SCIPsetDebugMsgPrint(set, "Nonzeros in resolved row: %d \n", resolvedrow->nnz);

   /* check if the quotient of coefficients in the resolvent exceeds the max allowed quotient */
   resolvedrow->coefquotient = (resolvedrow->nnz > 0) ? fabs(largestcoef / smallestcoef) : 0.0;
   if ( SCIPsetIsGT(set, resolvedrow->coefquotient, set->conf_generalresminmaxquot) )
   {
      if( !conflict->haslargecoef )
      {
         conflict->haslargecoef = TRUE;
         conflict->nreslargecoefs++;
      }
      SCIPsetDebugMsgPrint(set, "Quotient %f exceeds max allowed quotient", (resolvedrow->nnz > 0) ? fabs(largestcoef / smallestcoef) : 0.0);
      return SCIP_OKAY;
   }

   *success = TRUE;

   return SCIP_OKAY;
}

/** clause based resolution */
static
SCIP_RETCODE resolveClauses(
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   int                   residx,             /**< index of variable to resolve */
   int                   nvars,              /**< number of variables */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< array of indices of fixed variables */
   SCIP_Bool*            success             /**< apply resolution */
   )
{
   SCIP_Bool successclause;

   SCIPsetDebugMsgPrint(set, "Apply clause weakening for both conflict and reason \n");

   *success = FALSE;

   if( !set->conf_clausegenres && !set->conf_clausefallback )
      return SCIP_OKAY;

   /* first construct a conflict clause out of fixed bounds, current bound to
    * resolve, and bounds in the queue */
   SCIP_CALL( getConflictClause(conflict, blkmem, set, vars, currbdchginfo, &successclause, fixbounds, fixinds, nvars, FALSE) );
   if( successclause)
   {
      SCIP_CONFLICTROW* conflictrow;
      SCIP_CONFLICTROW* reasonrow;

      conflictrow = conflict->conflictrow;
      reasonrow = conflict->reasonrow;
      SCIPdebug(printConflictRow(conflict->conflictrow, set, vars, CLAUSAL_CONFLICT_ROWTYPE));

      assert(SCIPsetIsRelEQ(set, getSlackConflictRow(set, vars, conflictrow, currbdchginfo, fixbounds, fixinds), -1.0));
      conflictrow->slack = -1.0;
      /* get reason clause by resolving propagation */
      SCIP_CALL( getReasonClause(conflict, blkmem,  set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), 0, &successclause) );
      if (successclause)
      {
         int newsize;

         SCIPdebug(printConflictRow(reasonrow, set, vars, CLAUSAL_REASON_ROWTYPE));
         assert(SCIPsetIsRelEQ(set, getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds), 0.0));
         reasonrow->slack = 0.0;

         /* make sure enough memory is allocated for the resolved conflict row */
         newsize = conflictrow->nnz + reasonrow->nnz;
         if ( conflictrow->size < newsize )
         {
            SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &conflictrow->inds, conflictrow->size, newsize ) );
            conflictrow->size = newsize;
         }

         SCIP_CALL( linearCombRows(set, conflictrow, reasonrow, 1.0) );
         conflictRowRemoveZeroVars(conflictrow, set);

         assert(conflictrow->vals[residx] == 0);

         *success = TRUE;
      }
      else
         return SCIP_OKAY;
   }
   else
      return SCIP_OKAY;


   return SCIP_OKAY;
}

/** Apply weakening based reduction:
 * - Iteratively weaken variables from the reason row that do not affect the
 *   slack.
 * - Then apply CG/MIR/Coef.Tightening to reduce the slack.
 * - We weaken a variable if:
 *   * (Only for MIR/CG) it is not divisible by the coefficient in the conflict reason row AND
 *    * it is free (favor this case), or
 *    * it has a positive coefficient and its local upper bound is equal to the
 *      global upper bound, or
 *    * it has a negative coefficient and its local lower bound is equal to the
 *      global lower bound
 * - We weaken as long as the resolved slack is positive.
 * Remark: For binary variables, the slack of the resolvent should become negative
 *         at the latest when no more variables can be weakened.
*/
static
SCIP_RETCODE WeakeningBasedReduction(
   SCIP_CONFLICT *       conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_CONFLICTROW*     rowtoreduce,        /**< row to reduce */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< current bound change to resolve */
   int                   residx,             /**< index of variable to resolve */
   SCIP_Real*            fixbounds,          /**< array of fixed bounds */
   int*                  fixinds             /**< array of indices of fixed variables */
   )
{
   SCIP_Real previousslack;
   SCIP_Real resolutionslack;
   SCIP_CONFLICTROW* conflictrow;
   SCIP_CONFLICTROW* resolvedconflictrow;
   SCIP_CONFLICTROW* reducedreason;
   SCIP_BDCHGIDX* currbdchgidx;

   SCIP_Bool varwasweakened;
   int nvarsweakened;
   int totaltoweaken;
   int i;
   SCIP_Real coefinreason;

   assert(conflict != NULL);

   currbdchgidx = SCIPbdchginfoGetIdx(currbdchginfo);

   conflictrow = conflict->conflictrow;
   resolvedconflictrow = conflict->resolvedconflictrow;
   /* the reducedreason is our working row and replaces rowtoreduce only if resolution is guarranteed */
   conflictRowCreate(&reducedreason, blkmem);
   conflictRowCopy(&reducedreason, blkmem, rowtoreduce);

   i = 0;
   nvarsweakened = 0;
   totaltoweaken = 0;
   previousslack = rowtoreduce->slack;

   assert(SCIPsetIsGT(set, previousslack, 0.0));

   coefinreason = fabs(rowtoreduce->vals[residx]);
   assert(!SCIPsetIsZero(set, coefinreason));

   resolutionslack = conflict->resolvedconflictrow->slack;

   if( SCIPsetIsGE(set, resolutionslack, 0.0) )
   {
      SCIP_CALL( reasonRowSortWeakeningOrder(rowtoreduce, set, vars, currbdchgidx, residx, &totaltoweaken, set->conf_reductiontechnique == 'c' ? FALSE : TRUE, coefinreason) );
   }
   int nnzs = rowtoreduce->nnz;
   while ( SCIPsetIsGE(set, resolutionslack, 0.0) && nvarsweakened < totaltoweaken )
   {
      SCIP_VAR* vartoweaken;

      i = nnzs - nvarsweakened - 1;

      vartoweaken = vars[rowtoreduce->inds[i]];

      assert(rowtoreduce->inds[i] != residx);

      if( rowtoreduce->vals[rowtoreduce->inds[i]] > 0.0 )
      {
         SCIP_Real ub;
         ub = SCIPgetVarUbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);
         assert(SCIPsetIsEQ(set, ub, SCIPvarGetUbGlobal(vartoweaken)));
         weakenVarConflictRow(rowtoreduce, set, vartoweaken, i);
         varwasweakened = TRUE;
         ++nvarsweakened;
      }
      else if( rowtoreduce->vals[rowtoreduce->inds[i]] < 0.0 )
      {
         SCIP_Real lb;
         lb = SCIPgetVarLbAtIndex(set->scip, vartoweaken, currbdchgidx, TRUE);
         assert(SCIPsetIsEQ(set, lb, SCIPvarGetLbGlobal(vartoweaken)));
         weakenVarConflictRow(rowtoreduce, set, vartoweaken, i);
         varwasweakened = TRUE;
         ++nvarsweakened;
      }
      if (varwasweakened)
      {
         if ( !set->conf_weakenreasonall )
         {
            int nchgcoefs = 0;
            SCIP_Bool cutfound = FALSE;
            /* todo extend Chvatal-Gomory and MIR for constraints with general integer variables */
            if (!isBinaryConflictRow(set, vars, rowtoreduce) && set->conf_reductiontechnique != 'c')
            {
               SCIPsetDebugMsg(set, "Normalized MIR is only implemented for binary constraints\n");
               SCIPconflictRowFree(&reducedreason, blkmem);
               return SCIP_OKAY;
            }
            SCIP_CALL( conflictRowReplace(reducedreason, blkmem, rowtoreduce) );

            /* apply the chosen reduction technique */
            if (set->conf_reductiontechnique == 'd')
            {
               /* not implemented yet */
               assert(FALSE);
            }
            else if( set->conf_reductiontechnique == 'm')
               SCIP_CALL( NormalizedMirLhs(set, vars, reducedreason, coefinreason, &cutfound) );
            else
            {
               assert(set->conf_reductiontechnique == 'c');
               SCIP_CALL( tightenCoefs(set, vars, FALSE, reducedreason->vals, reducedreason->inds,
                              &reducedreason->nnz, &reducedreason->lhs, &nchgcoefs, TRUE, NULL) );
            }
            /* todo the update of the slack should be done in the coefTight/Division/MIR algorithm */
            if(cutfound || nchgcoefs > 0)
            {
               SCIP_Bool successresolution;
               reducedreason->slack = getSlackConflictRow(set, vars, reducedreason, currbdchginfo, fixbounds, fixinds);
               SCIPsetDebugMsg(set, "Slack before reduction: %g \n",  previousslack);
               SCIPsetDebugMsg(set, "Slack after reduction: %g \n", reducedreason->slack);
               SCIP_CALL( rescaleAndResolve(set, conflict, conflictrow, reducedreason, resolvedconflictrow, currbdchginfo, blkmem, residx, &successresolution) );
               resolutionslack = getSlackConflictRow(set, vars, resolvedconflictrow, currbdchginfo, fixbounds, fixinds);
               if(SCIPsetIsLT(set, resolutionslack, 0.0))
                  SCIP_CALL( conflictRowReplace(rowtoreduce, blkmem, reducedreason) );
            }
         }
      }
      else
      {
         assert(nvarsweakened == totaltoweaken);
         break;
      }
   }
   if( nvarsweakened > 0)
   {
      conflict->nreductioncalls++;
      if (set->conf_weakenreasonall)
         conflict->weakeningsumperc++;
      else
         conflict->weakeningsumperc += (SCIP_Real) nvarsweakened / totaltoweaken;
   }

   /* apply a division cut (CG or MIR) after weakening as much as possible, or if we did not weaken at all */
   if ( set->conf_weakenreasonall || nvarsweakened == 0 )
   {
      int nchgcoefs = 0;
      SCIP_Bool cutfound = FALSE;

      /* todo extend Chvatal-Gomory and MIR for constraints with general integer variables */
      if (!isBinaryConflictRow(set, vars, rowtoreduce) && set->conf_reductiontechnique != 'c')
      {
         SCIPsetDebugMsg(set, "Normalized MIR is only implemented for binary constraints\n");
         SCIPconflictRowFree(&reducedreason, blkmem);
         return SCIP_OKAY;
      }
      SCIP_CALL( conflictRowReplace(reducedreason, blkmem, rowtoreduce) );
      /* apply the chosen reduction technique */
      if (set->conf_reductiontechnique == 'd')
      {
         /* not implemented yet */
         assert(FALSE);
      }
      else if( set->conf_reductiontechnique == 'm')
         SCIP_CALL( NormalizedMirLhs(set, vars, reducedreason, coefinreason, &cutfound) );
      else
      {
         assert(set->conf_reductiontechnique == 'c');
         SCIP_CALL( tightenCoefs(set, vars, FALSE, reducedreason->vals, reducedreason->inds,
                        &reducedreason->nnz, &reducedreason->lhs, &nchgcoefs, TRUE, NULL) );
      }
      /* refactortodo the update of the slack should be included in the division algorithms */
      reducedreason->slack = getSlackConflictRow(set, vars, reducedreason, currbdchginfo, fixbounds, fixinds);
      SCIPsetDebugMsg(set, "Slack before reduction: %g \n",  previousslack);
      SCIPsetDebugMsg(set, "Slack after reduction: %g \n", reducedreason->slack);
      SCIP_CALL( conflictRowReplace(rowtoreduce, blkmem, reducedreason) );
   }

   SCIP_UNUSED(previousslack);
   SCIPconflictRowFree(&reducedreason, blkmem);
   return SCIP_OKAY;
}


/* reduce the reason constraint */
static
SCIP_RETCODE reduceReason(
   SCIP_CONFLICT *       conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_CONFLICTROW*     rowtoreduce,         /**< the row to reduce */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< current bound change to resolve */
   int                   residx,             /**< index of variable to resolve */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds             /**< array of indices of fixed variables */
)
{
   assert(conflict != NULL);

   SCIP_Real coefinrow;
   SCIP_BDCHGIDX* currbdchgidx;

   currbdchgidx = SCIPbdchginfoGetIdx(currbdchginfo);

   coefinrow = fabs(rowtoreduce->vals[residx]);

   if (set->conf_reductiontechnique == 's')
   {
      SCIPsetDebugMsgPrint(set, "Apply Complemented 0-1 Chvatal-Gomory since slack of resolved row: %f >= 0 \n", conflict->resolvedconflictrow->slack);
      SCIP_CALL( ComplementedChvatalGomoryLhs(set, vars, rowtoreduce, fixbounds, fixinds, currbdchgidx, residx, coefinrow) );
   }

   else if(set->conf_reductiontechnique == 'r')
   {
      SCIPsetDebugMsgPrint(set, "Apply Complemented 0-1 MIR since slack of resolved row: %f >= 0 \n", conflict->resolvedconflictrow->slack);
      SCIP_CALL( ComplementedMirLhs(set, vars, rowtoreduce, fixbounds, fixinds, currbdchgidx, residx, coefinrow) );
   }

   else
   {
      WeakeningBasedReduction(conflict, set, vars, blkmem, rowtoreduce, currbdchginfo, residx, fixbounds, fixinds);
   }

   /* check that the variable we are resolving is still in the reason row */
   assert( !SCIPsetIsZero(set, rowtoreduce->vals[residx]) );

   /* check that when we weaken as much as possible and the reason row is binary, the slack is at most zero */
   assert(!(set->conf_weakenreasonall) || !isBinaryConflictRow(set, vars, rowtoreduce) || SCIPsetIsLE(set, getSlackConflictRow(set, vars, rowtoreduce, currbdchginfo, fixbounds, fixinds), 0.0));

   if(getSlackConflictRow(set, vars, rowtoreduce, currbdchginfo, fixbounds, fixinds) < 0.0)
   {
      /* Mixedbinarytodo: Use this as conflict (think of the correct depth) */
      SCIPsetDebugMsg(set, "SLACK OF REDUCED ROW IS NEGATIVE: %g \n", getSlackConflictRow(set, vars, rowtoreduce, currbdchginfo, fixbounds, fixinds));
   }

   SCIPdebug(printConflictRow(rowtoreduce, set, vars, REDUCED_REASON_ROWTYPE));

#ifdef SCIP_CONFGRAPH_DOT
      confgraphAddRow(conflict, REDUCED_REASON_ROW);
#endif

   return SCIP_OKAY;
}

/** reason row from an LP row */
static
SCIP_RETCODE reasonRowFromLpRow(
   SCIP_SET*             set,                /**< global SCIP settings */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_ROW*             row,                /**< row to add */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row */
   SCIP_BDCHGINFO*       bdchginfo           /**< bound change to resolve */
   )
{
   SCIP_COL** cols;
   SCIP_VAR* vartoresolve;
   SCIP_Real* vals;
   int* inds;
   SCIP_Real lhs;
   SCIP_Real rowlhs;
   SCIP_Real rowrhs;
   int nnz;
   int varidx;
   int i;
   SCIP_Bool changesign;
   SCIP_Bool isincon;

   assert(reasonrow != NULL);
   assert(set != NULL);

   nnz = SCIProwGetNNonz(row);
   assert(nnz > 0);

   rowlhs = SCIProwGetLhs(row) - SCIProwGetConstant(row);
   rowrhs = SCIProwGetRhs(row) - SCIProwGetConstant(row);

   vartoresolve = SCIPbdchginfoGetVar(bdchginfo);
   varidx = SCIPvarGetProbindex(vartoresolve);

   vals = SCIProwGetVals(row);
   cols = SCIProwGetCols(row);

   SCIP_CALL( SCIPsetAllocBufferArray(set, &inds, nnz ) );

   isincon = FALSE;
   changesign = FALSE;
   for( i = 0; i < nnz; i++ )
   {
      SCIP_VAR* var;

      var = SCIPcolGetVar(cols[i]);
      inds[i] = SCIPvarGetProbindex(var);
      if ( inds[i] == varidx )
      {
         assert(var == vartoresolve);
         if ( (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER && vals[i] < 0)
           || (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER && vals[i] > 0) )
         {
            if (strcmp(SCIPconshdlrGetName(bdchginfo->inferencedata.reason.cons->conshdlr), "knapsack") != 0)
               assert(!SCIPsetIsInfinity(set, rowrhs));
            changesign = TRUE;
         }
         else if ( (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER && vals[i] > 0)
                || (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER && vals[i] < 0) )
         {
            if (strcmp(SCIPconshdlrGetName(bdchginfo->inferencedata.reason.cons->conshdlr), "knapsack") != 0)
               assert(!SCIPsetIsInfinity(set, -rowlhs));
            changesign = FALSE;
         }
         else
         {
            assert(FALSE);
         }
         isincon = TRUE;
      }
   }
   assert(isincon);
   SCIP_UNUSED(isincon);

   if ( changesign )
      lhs = -rowrhs;
   else
      lhs = rowlhs;

   assert(inds != NULL);
   assert(vals != NULL);

   SCIP_CALL( conflictRowAddSemiSparseData(reasonrow, blkmem, vals, inds, nnz, lhs, changesign) );

   SCIPsetFreeBufferArray(set, &inds);

   return SCIP_OKAY;
}

/** add a row to the conflict row */
static
SCIP_RETCODE conflictRowFromLpRow(
   SCIP_SET*             set,                /**< global SCIP settings */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_ROW*             row,                /**< row to add */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row */
   SCIP_BDCHGINFO*       bdchginfo,          /**< bound change to resolve */
   SCIP_Bool             frominfeaslp        /**< whether the row is from the infeasible LP or pseudoobj */
   )
{
   SCIP_COL** cols;
   SCIP_VAR* var;
   SCIP_Real* vals;
   int* inds;
   SCIP_Real lhs;
   SCIP_Real rowlhs;
   SCIP_Real rowrhs;
   int nnz;
   int varidx;
   int i;
   SCIP_Bool changesign;
   SCIP_Bool isincon;
   assert(conflictrow != NULL);
   assert(set != NULL);

   nnz = SCIProwGetNNonz(row);
   assert(nnz > 0);

   rowlhs = SCIProwGetLhs(row) - SCIProwGetConstant(row);
   rowrhs = SCIProwGetRhs(row) - SCIProwGetConstant(row);

   if(!frominfeaslp)
   {
      var = SCIPbdchginfoGetVar(bdchginfo);
      varidx = SCIPvarGetProbindex(var);
   }

   vals = SCIProwGetVals(row);
   cols = SCIProwGetCols(row);

   SCIP_CALL( SCIPsetAllocBufferArray(set, &inds, nnz ) );

   isincon = FALSE;
   changesign = FALSE;
   for( i = 0; i < nnz; i++ )
   {
      var = SCIPcolGetVar(cols[i]);
      inds[i] = SCIPvarGetProbindex(var);
      if ( !frominfeaslp && inds[i] == varidx )
      {
         if ( (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER && vals[i] > 0) ||
              (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER && vals[i] < 0) )
         {
            changesign = TRUE;
         }
         else if ( (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER && vals[i] < 0) ||
                   (SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER && vals[i] > 0) )

         {
            changesign = FALSE;
         }
         else
         {
            assert(FALSE);
         }
         isincon = TRUE;
      }
   }

   if(!frominfeaslp)
   {
      assert(isincon);
      SCIP_UNUSED(isincon);
   }

   if ( changesign )
      lhs = -rowrhs;
   else
      lhs = rowlhs;

   assert(inds != NULL);
   assert(vals != NULL);

   SCIP_CALL( conflictRowAddSemiSparseData(conflictrow, blkmem, vals, inds, nnz, lhs, changesign) );

   SCIPsetFreeBufferArray(set, &inds);

   return SCIP_OKAY;
}

/**
 * get the reason for the given bound change
 */
static
SCIP_RETCODE getReasonRow(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   SCIP_CONFLICTROW*     reasonrow,          /**< reason row for the bound change */
   int                   residx,             /**< index of the bound change to resolve */
   int                   validdepth,         /**< minimal depth level at which the conflict is valid */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< array of indices of fixed variables */
   SCIP_Bool*            success             /**< pointer to store whether we could get a linear reason */
)
{

   assert(success !=  NULL);
   assert(reasonrow != NULL);

   *success = FALSE;

   if (bdchginfoIsResolvable(currbdchginfo) && SCIPbdchginfoGetChgtype(currbdchginfo) == SCIP_BOUNDCHGTYPE_CONSINFER)
   {
         SCIP_CONS* reasoncon;
         SCIP_ROW* reasonlprow;
         reasoncon = SCIPbdchginfoGetInferCons(currbdchginfo);

         if(!SCIPconsIsGlobal(reasoncon))
         {
            SCIPsetDebugMsgPrint(set, "Reason constraint is not global \n");
            return SCIP_OKAY;
         }

         /* get the corresponding reason row */
         reasonlprow = SCIPconsCreateRow(set->scip, reasoncon);

         /* in case of orbitope-, orbisack-, and-constaints we construct a linearized clause as reason */
         if( reasonlprow == NULL || set->conf_clausegenres)
         {
               SCIP_CALL( getReasonClause(conflict, blkmem, set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), validdepth, success) );
               if (*success)
               {
                  assert(SCIPsetIsZero(set, getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds)));
                  reasonrow->slack = 0.0;
                  return SCIP_OKAY;
               }
               else
                 return SCIP_OKAY;
         }

         /* get the conflict row of the reason row */
         SCIP_CALL( reasonRowFromLpRow(set, blkmem, reasonlprow, reasonrow, currbdchginfo) );
         *success = TRUE;
         /* it may happen that some specialized propagation took place and the real reason is not the constraint
            e.g. negated cliques in cons_knapsack or ranged row propagation in cons_linear. */

         /* this happens if the reason is a negated clique found in the knapsack constraint handler */
         if (strcmp(SCIPconshdlrGetName(reasoncon->conshdlr), "knapsack") != 0)
         {
            assert(!SCIPsetIsInfinity(set, -reasonrow->lhs) || !SCIPsetIsInfinity(set, reasonrow->lhs));
         }
         else if(SCIPsetIsInfinity(set, -reasonrow->lhs) || SCIPsetIsInfinity(set, reasonrow->lhs))
         {
            /* to be able to continue we construct a linearized clause as reason */
            SCIP_CALL( getReasonClause(conflict, blkmem, set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), validdepth, success) );
            if (*success)
            {
               assert(SCIPsetIsZero(set, getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds)));
               reasonrow->slack = 0.0;
            }
            return SCIP_OKAY;

         }
         reasonrow->slack = getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds);

         /* If the slack is greater than 0, we check that the reason actually
         propagated the variable we resolve. It propagates a variable x_i if
         (slack - a_i * (oldbound - newbound) is smaller than 0 */
         if (SCIPsetIsGT(set, reasonrow->slack, 0.0))
         {
            SCIP_VAR* var;
            SCIP_BDCHGIDX* currbdchgidx;
            SCIP_Real coef;
            SCIP_Real boundusedinslack;

            currbdchgidx = SCIPbdchginfoGetIdx(currbdchginfo);
            var = SCIPbdchginfoGetVar(currbdchginfo);
            assert(var != NULL);
            assert(SCIPvarGetProbindex(var) == residx);

            coef = reasonrow->vals[residx];
            boundusedinslack = coef > 0 ? SCIPgetVarUbAtIndex(set->scip, var, currbdchgidx, TRUE) : SCIPgetVarLbAtIndex(set->scip, var, currbdchgidx, TRUE);

            if (!SCIPsetIsLT(set, reasonrow->slack - coef * ( boundusedinslack - SCIPbdchginfoGetOldbound(currbdchginfo) ) , 0.0))
            {

               assert( (strcmp(SCIPconshdlrGetName(reasoncon->conshdlr), "knapsack") == 0) || (strcmp(SCIPconshdlrGetName(reasoncon->conshdlr), "linear") == 0) );
               SCIP_CALL( getReasonClause(conflict, blkmem, set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), validdepth, success) );
               if (*success)
               {
                  assert(SCIPsetIsZero(set, getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds)));
                  reasonrow->slack = 0.0;
               }
               return SCIP_OKAY;
            }
         }
   }
   else if (bdchginfoIsResolvable(currbdchginfo) && SCIPbdchginfoGetChgtype(currbdchginfo) == SCIP_BOUNDCHGTYPE_PROPINFER)
   {
      SCIP_CALL( getReasonClause(conflict, blkmem,  set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), validdepth, success) );
      if (*success)
      {
         assert(SCIPsetIsZero(set, getSlackConflictRow(set, vars, reasonrow, currbdchginfo, fixbounds, fixinds)));
         reasonrow->slack = 0.0;
      }
      return SCIP_OKAY;

   }
   else
   {
      SCIPsetDebugMsgPrint(set, "Could not obtain a reason row \n");
      *success = FALSE;
   }
   return SCIP_OKAY;
}

/**
 * get the conflict row for the given bound change
 * - it is either an LP row
 * - or a weakened LP row (if the LP row is too long)
 * - or a clause (if no LP row is available, or if the slack of the LP row not
 *   negative. This can happen if we used some non linear argument when
 *   propagating the rows)
 */
static
SCIP_RETCODE getConflictRow(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_PROB*            prob,               /**< problem data */
   SCIP_ROW*             initialconflictrow, /**< row of constraint that detected the conflict */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   int                   residx,             /**< index of the bound change to resolve */
   int                   maxsize,            /**< maximal size of conflict rows */
   SCIP_Bool*            success             /**< pointer to store whether we could get a conflict row */
)
{
   SCIP_VAR** vars;
   SCIP_CONFLICTROW* conflictrow;
   SCIP_BDCHGIDX* currbdchgidx;

   vars = prob->vars;
   assert(vars != NULL);

   conflictrow = conflict->conflictrow;
   currbdchgidx = SCIPbdchginfoGetIdx(currbdchginfo);
   assert(currbdchgidx != NULL);

   *success = FALSE;
   /* first try to create the conflict row from the infeasible LP row */
   if (initialconflictrow != NULL && !set->conf_clausegenres)
   {
      SCIPsetDebugMsgPrint(set, "Initial LP Row: %s \n", SCIProwGetName(initialconflictrow));
      SCIP_CALL( conflictRowFromLpRow(set, blkmem, initialconflictrow, conflictrow, currbdchginfo, FALSE) );

      /* if the conflict row is too large, we try to weaken it */
      if( conflictrow->nnz > maxsize )
      {
         assert( conflictrow->nnz == SCIProwGetNNonz(initialconflictrow) );
         SCIPsetDebugMsgPrint(set, "Number of nonzeros in conflict is larger than maxsize %d > %d\n", conflictrow->nnz, maxsize);
         SCIPsetDebugMsgPrint(set, "Try to shorten the conflict row by applying weakening \n");

         weakenConflictRow(conflictrow, set, vars, currbdchgidx, NULL, NULL);

         if(conflictrow->nnz > maxsize)
         {
            SCIPsetDebugMsgPrint(set, "Conflict row is still too large after weakening %d > %d\n", conflictrow->nnz, maxsize);
            conflict->nreslongconfs++;
            return SCIP_OKAY;
         }
      }

      /* set the slack */
      conflictrow->slack = getSlackConflictRow(set, vars, conflictrow, currbdchginfo, NULL, NULL);
   }

   /** if no row exists create the conflict row (if possible) from the bound changes that lead to infeasibility
    * Moreover the slack should be negative. If it is not (for some good reason) create again the conflict row
    * (if possible) from the bound changes.
    * The only cases where this may not be true is if the conflict is found by some non-linear propagation argument:
    *  - by a negated clique in the knapsack constraint handler
    *  - by propagating a ranged row (gcd argument)
    */
   if ( set->conf_clausegenres || initialconflictrow == NULL || SCIPsetIsGE(set, conflictrow->slack, 0.0) )
   {
      SCIP_Bool successclause;
      SCIP_CONSHDLR* conshdlr;

      if (initialconflictrow != NULL && !set->conf_clausegenres)
      {
         SCIPsetDebugMsgPrint(set, "Slack of conflict constraint is not negative \n");
         conshdlr = SCIProwGetOriginConshdlr(initialconflictrow);
         SCIPsetDebugMsgPrint(set, "%s",SCIPconshdlrGetName(conshdlr));
         /* relaxed assertion */
         assert(strcmp(SCIPconshdlrGetName(conshdlr), "knapsack") == 0 || strcmp(SCIPconshdlrGetName(conshdlr), "linear") == 0);
      }

      SCIP_CALL( getConflictClause(conflict, blkmem, set, vars, currbdchginfo, &successclause, NULL, NULL, prob->nvars, TRUE) );
      if (!successclause)
      {
         SCIPsetDebugMsgPrint(set, "Initial conflict clause could not be retrieved \n");
         return SCIP_OKAY;
      }
      conflictrow->slack = getSlackConflictRow(set, vars, conflictrow, currbdchginfo, NULL, NULL);
      assert( conflictrow->slack == -1.0 );
   }

   /* check once more if the initial conflict is too long */
   if(conflictrow->nnz > maxsize)
   {
      conflict->nreslongconfs++;
      return SCIP_OKAY;
   }

   assert(SCIPsetIsLT(set, conflictrow->slack, 0.0));
   *success = TRUE;

   return SCIP_OKAY;
}

/** execute resolution; reduce reason if necessary */
static
SCIP_RETCODE executeResolutionStep(
   SCIP_CONFLICT *       conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< current bound change to resolve */
   int                   residx,             /**< index of variable to resolve */
   int                   validdepth,         /**< minimal depth level at which the conflict is valid */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< array of indices of fixed variables */
   SCIP_Bool*            successresolution   /**< pointer to store whether the resolution was successful */
   )
{
   SCIP_CONFLICTROW* conflictrow;
   SCIP_CONFLICTROW* reasonrow;
   SCIP_CONFLICTROW* reducedreasonrow;
   SCIP_CONFLICTROW* resolvedconflictrow;

   int nvars;

   assert(conflict != NULL);

   conflictrow = conflict->conflictrow;
   reasonrow = conflict->reasonrow;
   reducedreasonrow = conflict->reducedreasonrow;
   resolvedconflictrow = conflict->resolvedconflictrow;

   *successresolution = FALSE;

   nvars = SCIPgetNVars(set->scip);
   assert(!SCIPsetIsZero(set, reasonrow->vals[residx]));

   /* try to resolve without reducing the reason row */
   SCIPsetDebugMsgPrint(set, "Resolve %s without reducing the reason row \n", SCIPvarGetName(vars[residx]));
   SCIP_CALL( rescaleAndResolve(set, conflict, conflictrow, reasonrow, resolvedconflictrow, currbdchginfo, blkmem,
                        residx, successresolution) );

   if (!(*successresolution))
      return SCIP_OKAY;


   resolvedconflictrow->slack = getSlackConflictRow(set, vars, resolvedconflictrow, currbdchginfo, fixbounds, fixinds);

   /* if the resolvent is not infeasible under the local domain, try to reduce the reason row */
   if( SCIPsetIsGE(set, resolvedconflictrow->slack, 0.0) )
   {
      /* if we do not want to apply any reduction to the reason we just abort */
      if( set->conf_reductiontechnique == 'o' )
         return SCIP_OKAY;

      /* MixedBinaryTodo: In case of mixed binary copy the original reason here */
      conflictRowReplace(reducedreasonrow, blkmem, reasonrow);

      /* apply reduction to the reason row */
      SCIP_CALL( reduceReason(conflict, set, blkmem, vars, reducedreasonrow, currbdchginfo, residx, fixbounds, fixinds));

      /* MixedBinaryTodo add some flag if the reduction really did something so that we avoid some of the calculations below */
      /* after reduction resolve again */
      SCIPsetDebugMsgPrint(set, "Resolve %s after reducing the reason row \n", SCIPvarGetName(vars[residx]));
      SCIP_CALL( rescaleAndResolve(set, conflict, conflictrow, reducedreasonrow, resolvedconflictrow, currbdchginfo, blkmem,
                              residx, successresolution) );

      if (!(*successresolution))
         return SCIP_OKAY;

      resolvedconflictrow->slack = getSlackConflictRow(set, vars, resolvedconflictrow, currbdchginfo, fixbounds, fixinds);

      /* mixed binary case reduction */
      if( SCIPsetIsGE(set, resolvedconflictrow->slack, 0.0) )
      {
         SCIP_BDCHGINFO* continuousbdchginfo;
         SCIP_Bool successgetreason;
         SCIP_Real coefresidx;
         /* now both the reducedreasonrow and reasonrow are the rows before the reduction */
         /* todo add a parameter to be able to work with the row after the reduction */
         conflictRowReplace(reducedreasonrow, blkmem, reasonrow);

         coefresidx = reducedreasonrow->vals[residx];
         /* In the following the reducedreasonrow is the aggregation of the different reasonrows of continuous bound changes */
         SCIP_CALL( slackReducingContinuousBdchgQueue(conflict, set, vars, reducedreasonrow, SCIPbdchginfoGetIdx(currbdchginfo)) );
         /** MixedBinaryTodo: reduce the reason again after removing all continuous variables
          * - Check if the reason contrains the variable we are resolving
          * - Also check the case where the reason becomes infeasible and hence can be considered as the conflict constraint
          * - Apply reduction to the reason row
          */
         if (SCIPpqueueNElems(conflict->continuousbdchgqueue) > 0)
         {
            SCIPsetDebugMsgPrint(set, "Slack-reducing continuous variables in the reason row \n");
            SCIPdebug(printConflictRow(reducedreasonrow, set, vars, REASON_ROWTYPE));
         }

         while( SCIPpqueueNElems(conflict->continuousbdchgqueue) > 0 )
         {
            SCIPdebug(printAllBoundChanges(conflict, set, TRUE));
            continuousbdchginfo = (SCIP_BDCHGINFO*)(SCIPpqueueRemove(conflict->continuousbdchgqueue));
            assert(continuousbdchginfo != NULL);

            int varidx = SCIPvarGetProbindex(SCIPbdchginfoGetVar(continuousbdchginfo));
            /* get reason row of the latest bdchginfo */
            conflictRowClear(blkmem, reasonrow, nvars);
            SCIP_CALL( getReasonRow(conflict, blkmem, vars, set, continuousbdchginfo, reasonrow, varidx, validdepth, fixbounds, fixinds,
                                    &successgetreason) );
            if (!successgetreason)
               return SCIP_OKAY;

            SCIPdebug(printConflictRow(reasonrow, set, vars, CONTINUOUS_REASON_ROWTYPE));

            SCIPsetDebugMsgPrint(set, "Resolve slack-reducing continuous variable %s \n", SCIPvarGetName(vars[varidx]));
            /* resolve the continuous variable */
            SCIP_CALL( rescaleAndResolve(set, conflict, reducedreasonrow, reasonrow, resolvedconflictrow, currbdchginfo, blkmem,
                                 varidx, successresolution) );

            conflictRowReplace(reducedreasonrow, blkmem, resolvedconflictrow);

            if (coefresidx * reducedreasonrow->vals[residx] < 0.0)
            {
               SCIPsetDebugMsgPrint(set, "The sign of the coefficient of the variable we are resolving changed \n");
               SCIPdebug(printConflictRow(reducedreasonrow, set, vars, REDUCED_REASON_ROWTYPE));
               assert(FALSE);
            }
            SCIPdebug(printConflictRow(reducedreasonrow, set, vars, REDUCED_REASON_ROWTYPE));

            SCIP_CALL( slackReducingContinuousBdchgQueue(conflict, set, vars, reducedreasonrow, SCIPbdchginfoGetIdx(continuousbdchginfo)) );
         }
         /* after the loop we can weaken all continuous variables with their global bounds */
         SCIP_CALL( weakenContinuousVarsConflictRow(reducedreasonrow, set, vars, residx) );
         /* apply reduction to the reason row */
         SCIP_CALL( reduceReason(conflict, set, blkmem, vars, reducedreasonrow, currbdchginfo, residx, fixbounds, fixinds));
      }
      SCIPsetDebugMsgPrint(set, "Resolve %s after reducing the reason row \n", SCIPvarGetName(vars[residx]));
      /* after all reductions resolve one final time */
      SCIP_CALL( rescaleAndResolve(set, conflict, conflictrow, reducedreasonrow, resolvedconflictrow, currbdchginfo, blkmem,
                           residx, successresolution) );
      resolvedconflictrow->slack = getSlackConflictRow(set, vars, resolvedconflictrow, currbdchginfo, fixbounds, fixinds);
      if(resolvedconflictrow->slack > 0.0)
      {
         SCIP_CALL( getReasonClause(conflict, blkmem, set, currbdchginfo, SCIPbdchginfoGetNewbound(currbdchginfo), validdepth, successresolution) );
         if (*successresolution)
         {
            assert(SCIPsetIsZero(set, getSlackConflictRow(set, vars, resolvedconflictrow, currbdchginfo, fixbounds, fixinds)));
            resolvedconflictrow->slack = 0.0;
         }
         return SCIP_OKAY;
      }
   }

   return SCIP_OKAY;
}


static
SCIP_RETCODE fixBoundChangeWithoutResolving(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR**            vars,               /**< array of variables */
   SCIP_BDCHGINFO**      currbdchginfo,      /**< pointer to current bound change to resolve */
   int*                  currbdchgdepth,     /**< pointer to store the depth of the bound change */
   int                   nressteps,          /**< number of bound changes that have been resolved so far */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< dense array of indices of fixed variables */
   SCIP_Bool*            success             /**< pointer to store whether we could get a conflict row */
   )
{

      assert(conflict != NULL);
      assert(set != NULL);
      assert(vars != NULL);
      assert(*currbdchginfo != NULL);
      assert(currbdchgdepth != NULL);
      assert(fixbounds != NULL);
      assert(fixinds != NULL);

      *success = FALSE;
      if ( SCIPbdchginfoGetChgtype(*currbdchginfo) == SCIP_BOUNDCHGTYPE_CONSINFER )
      {
         SCIP_CONS* reasoncon;

         reasoncon = SCIPbdchginfoGetInferCons(*currbdchginfo);
         /* we resolve only with globally valid constraints */
         if(!SCIPconsIsGlobal(reasoncon))
            return SCIP_OKAY;
      }
      if( existsResolvablebdchginfo(conflict) )
      {
         SCIP_BOUNDTYPE boundtype;
         SCIP_BOUNDCHGTYPE bdchgtype;

         assert(SCIPsetIsLT(set, getSlackConflictRow(set, vars, conflict->conflictrow, *currbdchginfo, fixbounds, fixinds), 0.0));

         /* if a bound for the variable has already been ignored then abort */
         if( fixinds[SCIPvarGetProbindex(SCIPbdchginfoGetVar(*currbdchginfo))] != 0 )
            return SCIP_OKAY;

         boundtype = SCIPbdchginfoGetBoundtype(*currbdchginfo);
         bdchgtype = SCIPbdchginfoGetChgtype(*currbdchginfo);
         /* ignore the bound change and continue */
         fixinds[SCIPvarGetProbindex(SCIPbdchginfoGetVar(*currbdchginfo))] = boundtype == SCIP_BOUNDTYPE_UPPER ? 1 : -1;
         fixbounds[SCIPvarGetProbindex(SCIPbdchginfoGetVar(*currbdchginfo))] = SCIPbdchginfoGetNewbound(*currbdchginfo);
         /* we must reset the conflict lower and upper bound to be able to add weaker bounds later */
         if( SCIPbdchginfoGetBoundtype(*currbdchginfo) == SCIP_BOUNDTYPE_LOWER )
         {
            vars[SCIPvarGetProbindex(SCIPbdchginfoGetVar(*currbdchginfo))]->conflictreslb = SCIP_REAL_MIN;
         }
         else
         {
            assert(SCIPbdchginfoGetBoundtype(*currbdchginfo) == SCIP_BOUNDTYPE_UPPER);
            vars[SCIPvarGetProbindex(SCIPbdchginfoGetVar(*currbdchginfo))]->conflictresub = SCIP_REAL_MAX;
         }

         SCIPsetDebugMsgPrint(set, "ignoring the latest bound change of variable %s to %f \n", SCIPvarGetName(SCIPbdchginfoGetVar(*currbdchginfo)),
               SCIPbdchginfoGetNewbound(*currbdchginfo));


         if (nressteps == 0)
            SCIP_CALL( updateBdchgQueue(set, vars, conflict->conflictrow, SCIPbdchginfoGetIdx(*currbdchginfo)) );

         /* extract latest bound change from queue */
         *currbdchginfo = conflictRemoveCand(set, conflict, FALSE);
         assert(*currbdchginfo != NULL);

         /* if no resolution has been applied yet, and the bound change is a
         branching decision, we can ignore it and continue with the next
         bound change. Here we have to update the last bound change depth */
         if( nressteps == 0 && bdchgtype == SCIP_BOUNDCHGTYPE_BRANCHING )
         {
            *currbdchgdepth = SCIPbdchginfoGetDepth(*currbdchginfo);
         }
         /* In case we apply fix and continue */
         else if(!set->conf_fixandcontinue)
            return SCIP_OKAY;
         assert(SCIPsetIsLT(set, getSlackConflictRow(set, vars, conflict->conflictrow, *currbdchginfo, fixbounds, fixinds), 0.0));

      }
      else
         return SCIP_OKAY;

      *success = TRUE;
      return SCIP_OKAY;
}

/* add clause conflict */
static
SCIP_RETCODE addClauseConflict(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_PROB*            transprob,          /**< transformed problem */
   SCIP_PROB*            origprob,           /**< original problem */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_CONFLICTROW*     conflictrow,        /**< conflict row to add to the tree */
   SCIP_BDCHGINFO*       currbdchginfo,      /**< bound change to resolve */
   SCIP_Real*            fixbounds,          /**< dense array of fixed bounds */
   int*                  fixinds,            /**< dense array of indices of fixed variables */
   int*                  nconss,             /**< pointer to store the number of generated conflict constraints */
   int*                  nconfvars           /**< pointer to store the number of variables in generated conflict constraints */
   )
{
   SCIP_Bool success;

   getConflictClause(conflict, blkmem, set, transprob->vars, currbdchginfo, &success, fixbounds, fixinds, transprob->nvars, FALSE);

   if (success)
   {
      SCIPdebug(printConflictRow(conflict->conflictrow, set, transprob->vars, CLAUSAL_CONFLICT_ROWTYPE));
      assert(SCIPsetIsRelEQ(set, getSlackConflictRow(set, transprob->vars, conflict->conflictrow, currbdchginfo, fixbounds, fixinds), -1.0));
   }
   else
   {
      /* we increase the number of aborts for a known reason, since in this case no
       * conflict constraint is generated because of not being able to get a clause */
      if(set->conf_addclauseonly)
         conflict->nknownaborts++;
      return SCIP_OKAY;
   }

   if(SCIPdebugSolIsEnabled(set->scip))
      checkConflictDebugSol(conflict, blkmem, set, transprob->vars, stat, tree, conflict->conflictrow);

   SCIP_CALL(SCIPconflictAddConflictCon(conflict, blkmem, set, stat, transprob, origprob, tree, reopt, lp, branchcand, eventqueue, cliquetable, conflict->conflictrow, &success));

   if (success)
   {
      (*nconss)++;
      (*nconfvars) += conflict->conflictrow->nnz;
   }
   return SCIP_OKAY;
}

static
SCIP_RETCODE addConflictRows(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   SCIP_PROB*            transprob,          /**< transformed problem */
   SCIP_PROB*            origprob,           /**< original problem */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< current LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   int                   nconstoadd,         /**< number of conflict constraints to add */
   int*                  nconss,             /**< pointer to store the number of generated conflict constraints */
   int*                  nconfvars           /**< pointer to store the number of variables in generated conflict constraints */
   )
{
   int i;

   for( i = 0; i < nconstoadd; i++ )
   {
      SCIP_CONFLICTROW* conflictrowtoadd;

      conflictrowtoadd = conflict->conflictrows[i];
      assert(SCIPsetIsLT(set, conflictrowtoadd->slack, 0.0));

      if ( SCIPsetIsLT(set, conflictrowtoadd->coefquotient, set->conf_generalresminmaxquot) )
      {

         SCIP_Bool success;

         if(SCIPdebugSolIsEnabled(set->scip))
            checkConflictDebugSol(conflict, blkmem, set, transprob->vars, stat, tree, conflict->conflictrow);

         SCIP_CALL( SCIPconflictAddConflictCon(conflict, blkmem, set, stat, transprob, origprob, tree, reopt,
               lp, branchcand, eventqueue, cliquetable, conflictrowtoadd, &success) );
         if( success )
         {
            (*nconss)++;
            (*nconfvars) += conflictrowtoadd->nnz;
         }
      }
   }
   return SCIP_OKAY;
}

SCIP_RETCODE conflictAnalyzeResolution(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            transprob,          /**< transformed problem */
   SCIP_PROB*            origprob,           /**< original problem */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_ROW*             initialconflictrow, /**< row of constraint that detected the conflict */
   int                   validdepth,         /**< minimal depth level at which the initial conflict set is valid */
   SCIP_Bool             infeasibleLP,       /**< does the conflict originate from an infeasible LP? */
   SCIP_Bool             pseudoobj,          /**< does the conflict originate from a violated pseudo objective bound? */
   int*                  nconss,             /**< pointer to store the number of generated conflict constraints */
   int*                  nconfvars           /**< pointer to store the number of variables in generated conflict constraints */
   )
{
   SCIP_CONFLICTROW *conflictrow;
   SCIP_CONFLICTROW *resolvedconflictrow;
   SCIP_CONFLICTROW *reasonrow;

   SCIP_BDCHGINFO* bdchginfo;
   SCIP_BDCHGINFO* nextbdchginfo;
   SCIP_BDCHGIDX* bdchgidx;

   int bdchgdepth;
   int lastuipdepth;
   int focusdepth;
   int currentdepth;
   int maxvaliddepth;
   int maxsize;
   int nchgcoefs;
   int nressteps;
   int nfuips;
   SCIP_Real* cutcoefs;
   SCIP_Real* fixbounds;
   int* cutinds;
   int* fixinds;
   SCIP_Bool successresolution;
   SCIP_Bool successgetconflict;
   SCIP_Bool successgetreason;
   SCIP_Bool usescutoffbound;
   int nvars;
   int i;

   SCIP_VAR** vars;
   SCIP_VAR* vartoresolve;
   int residx;

   assert(conflict != NULL);
   assert(set != NULL);
   assert(stat != NULL);
   assert(0 <= validdepth && validdepth <= SCIPtreeGetCurrentDepth(tree));
   assert(nconss != NULL);
   assert(nconfvars != NULL);
   assert(conflict->nconflictrows == 0);

   focusdepth = SCIPtreeGetFocusDepth(tree);
   currentdepth = SCIPtreeGetCurrentDepth(tree);
   assert(currentdepth == tree->pathlen-1);
   assert(focusdepth <= currentdepth);

   lastuipdepth = -1;

   *nconss = 0;
   *nconfvars = 0;

   /** check, whether local conflicts are allowed; however, don't generate
    * conflict constraints that are only valid in the probing path and not in
    * the problem tree (i.e. that exceed the focusdepth)
    */
   maxvaliddepth = (set->conf_resallowlocal ? MIN(currentdepth-1, focusdepth) : 0);
   if( validdepth > maxvaliddepth )
   {
      conflict->nknownaborts++;
      return SCIP_OKAY;
   }

   vars = SCIPprobGetVars(transprob);
   nvars = transprob->nvars;

   /* todo update this for bound exceeding LPs (till now it works only for pseudoobj) */
   usescutoffbound = conflict->conflictrow->usescutoffbound;

   conflictrow = conflict->conflictrow;
   conflictrow->usescutoffbound = usescutoffbound;
   resolvedconflictrow = conflict->resolvedconflictrow;
   reasonrow = conflict->reasonrow;

   /* clear the conflict, reason, resolved conflict rows */
   conflictRowClear(blkmem, conflict->conflictrow, nvars);
   conflictRowClear(blkmem, conflict->resolvedconflictrow, nvars);
   conflictRowClear(blkmem, conflict->reasonrow, nvars);
   conflictRowClear(blkmem, conflict->reducedreasonrow, nvars);

   if( infeasibleLP || pseudoobj )
   {
      assert(SCIPpqueueNElems(conflict->resbdchgqueue) == 0);
      assert(SCIPpqueueNElems(conflict->resforcedbdchgqueue) == 0);

      if(SCIProwGetNNonz(initialconflictrow) == 0)
      {
         SCIPsetDebugMsgPrint(set, "Conflict analysis not applicable since no conflict row could be created \n");
         conflict->nknownaborts++;
         return SCIP_OKAY;
      }
      SCIP_CALL( conflictRowFromLpRow(set, blkmem, initialconflictrow, conflictrow, NULL, TRUE) );

      /* todo this can be avoided by using the same queues as graph conflict analysis */
      SCIP_CALL( updateBdchgQueue(set, vars, conflictrow, NULL) );
   }

   /* last bound change that led to infeasibility */
   bdchginfo = conflictFirstCand(set, conflict, TRUE);

   /* if no bound change exists or none of them was infered by a resolvable
   constraint then we terminate */
   if ( bdchginfo == NULL || !existsResolvablebdchginfo(conflict) )
   {
#ifdef SCIP_DEBUG
      /* if at least one bound change is in the queue, print them all */
      if(bdchginfo != NULL)
         printAllBoundChanges(conflict, set, FALSE);
#endif
      conflict->nknownaborts++;
      SCIPsetDebugMsgPrint(set, "Conflict analysis not applicable since no resolvable bounds exist \n");
      return SCIP_OKAY;
   }

   /* remove the last bound change */
   bdchginfo = conflictRemoveCand(set, conflict, TRUE);
   bdchgidx = SCIPbdchginfoGetIdx(bdchginfo);
   bdchgdepth = SCIPbdchginfoGetDepth(bdchginfo);

   vartoresolve = SCIPbdchginfoGetVar(bdchginfo);
   residx = SCIPvarGetProbindex(vartoresolve);

   /* check if the variable we are resolving is active */
   assert(SCIPvarIsActive(vartoresolve));

   /* calculate the maximal size of each accepted conflict set */
   maxsize = (int) (set->conf_maxvarsfracres * SCIPprobGetNVars(transprob));

   if( !infeasibleLP && !pseudoobj )
   {
      /* sets the initial conflict row for the bound change directly in conflict->conflictrow */
      getConflictRow(conflict, blkmem, set, transprob, initialconflictrow, bdchginfo, residx, maxsize, &successgetconflict);
      /* if we could not get the conflict row, then we abort */
      if( !successgetconflict )
      {
         SCIPsetDebugMsgPrint(set, "Conflict analysis not applicable since no conflict row could be created \n");
         conflict->nknownaborts++;
         return SCIP_OKAY;
      }
   }
   else
   {
      /* since the dual proof or pseudo objective may be very long we weaken them as much as we can */
      weakenConflictRow(conflictrow, set, vars, bdchgidx, NULL, NULL);
      if(conflictrow->nnz > maxsize)
      {
         conflict->nreslongconfs++;
         conflict->nknownaborts++;
         goto TERMINATE_RESOLUTION_LOOP;
      }
      /* set the slack */
      conflictrow->slack = getSlackConflictRow(set, vars, conflictrow, bdchginfo, NULL, NULL);
      assert(SCIPsetIsLT(set, conflictrow->slack, 0.0));
   }

   SCIP_CALL( conflictRowReplace(resolvedconflictrow, blkmem, conflictrow) );

   /* Apply coefficient tightening to the conflict constraint should never hurt */
   SCIP_CALL( tightenCoefs(set, vars, FALSE, conflictrow->vals, conflictrow->inds,
                              &conflictrow->nnz, &conflictrow->lhs, &nchgcoefs, TRUE, NULL) );

   if (nchgcoefs > 0)
   {
      SCIP_Real newslack;
       /* The new slack should always be less or equal to the old slack */
      newslack = getSlackConflictRow(set, vars, conflictrow, bdchginfo, NULL, NULL);
      assert(SCIPsetIsLE(set, newslack, conflictrow->slack + EPS)  || SCIPsetIsRelLE(set, newslack, conflictrow->slack));
      conflictrow->slack = newslack;
   }

   /* get the next bound change */
   nextbdchginfo = conflictFirstCand(set, conflict, FALSE);

   conflictrow->coefquotient = getQuotLargestSmallestCoef(set, conflictrow->inds, conflictrow->vals, conflictrow->nnz);

   nressteps = 0;
   nfuips = 0;

   /* allocate vectors for mir cuts */
   if( set->conf_applycmirreason || set->conf_applycmir || set->conf_applysimplemir)
   {
      SCIP_CALL( SCIPsetAllocBufferArray(set, &cutcoefs, nvars) );
      SCIP_CALL( SCIPsetAllocBufferArray(set, &cutinds, nvars) );
   }

   /* initialize indices and bounds for the unresolvable bound changes */
   SCIP_CALL( SCIPsetAllocBufferArray(set, &fixinds, nvars) );
   SCIP_CALL( SCIPsetAllocBufferArray(set, &fixbounds, nvars) );

   /** set value in fixinds to 0 to indicate that a variable is not fixed
    * if a variable is set at an upper bound, then the value is 1
    * if a variable is set at a lower bound, then the value is -1
    */
   for( i = 0; i < nvars; ++i )
      fixinds[i] = 0;

   /** main loop: All-FUIP RESOLUTION
    * --------------------------------
    * - we start with the initial conflict row and the first bound change to
    *   resolve
    * --------------------------------
    * - Fix & Continue: if we can't explain/resolve the bound change, i.e. the reason
    *   is a branching or non-linear then we ignore(fix) it and continue with the
    *   next bound change. We have to ignore all other bound changes for
    *   this variable (resolve with no-good)
    * - if the bound change is resolvable:
    *    - get the reason row for the bound change
    *    - apply CG/MIR/Coef.Tightening reduction to the reason
    *    - take the linear combination of the conflict row and the reason row
    *    - apply coefficient tightening to the resolved row (maybe also cMIR?)
    * - if there is no other bound change in the queue from the same depth level
    *   then we are at a UIP
    *    - keep this constraint and either terminate 1-FUIP resolution or continue
    *      with the next bound change
    */
   while( TRUE )  /*lint !e716*/
   {
#ifdef SCIP_CONFGRAPH_DOT
      {
         startSubgraph(nressteps + 1);
         confgraphAddBdchg(bdchginfo, CURRENT_BOUND_CHANGE);
         if (nextbdchginfo != NULL)
            confgraphAddBdchg(nextbdchginfo, NEXT_BOUND_CHANGE);
         confgraphAddRow(conflict, CONFLICT_ROW);
      }
#endif
#ifdef SCIP_DEBUG
      {
         SCIPsetDebugMsgPrint(set, "\nResolution Iteration: %d \n", nressteps);
         SCIPsetDebugMsgPrint(set, "Current bound change already removed from the queue: \n");
         printSingleBoundChange(set, bdchginfo);
         printAllBoundChanges(conflict, set, FALSE);
      }
#endif
      if(SCIPdebugSolIsEnabled(set->scip))
         checkConflictDebugSol(conflict, blkmem, set, vars, stat, tree, conflictrow);

      if( !bdchginfoIsResolvable(bdchginfo) )
      {
         SCIP_Bool successfixing;

#ifdef SCIP_DEBUG
         printNonResolvableReasonType(set, bdchginfo);
#endif
         SCIP_CALL( fixBoundChangeWithoutResolving(conflict, set, vars, &bdchginfo, &bdchgdepth, nressteps,
                        fixbounds, fixinds, &successfixing) );
         if( !successfixing )
         {
            conflict->nknownaborts++;
            goto TERMINATE_RESOLUTION_LOOP;
         }

         assert(bdchginfo != NULL);

         /* get the current bound change index */
         bdchgidx = SCIPbdchginfoGetIdx(bdchginfo);

         /* get next bound change from queue */
         nextbdchginfo = conflictFirstCand(set, conflict, FALSE);

         vartoresolve = SCIPbdchginfoGetVar(bdchginfo);
         /* check if the variable we are resolving is active */
         assert(SCIPvarIsActive(vartoresolve));

      }

      /* here is the generalized resolution iteration */
      else
      {
         residx = SCIPvarGetProbindex(vartoresolve);

         conflictRowClear(blkmem, conflict->reasonrow, nvars);

         /* get reason row of the latest bdchginfo */
         SCIP_CALL( getReasonRow(conflict, blkmem, vars, set, bdchginfo, conflict->reasonrow, residx, validdepth, fixbounds, fixinds,
                                 &successgetreason) );
         if( !successgetreason )
         {
            SCIPsetDebugMsgPrint(set, "Could not obtain reason row for bound change \n");
            conflict->nknownaborts++;
            goto TERMINATE_RESOLUTION_LOOP;
         }

         SCIPdebug(printConflictRow(conflict->conflictrow, set, vars, CONFLICT_ROWTYPE));
         SCIPdebug(printConflictRow(reasonrow, set, vars, REASON_ROWTYPE));

#ifdef SCIP_CONFGRAPH_DOT
            confgraphAddRow(conflict, REASON_ROW);
#endif
         successresolution = FALSE;

         /* call resolution */
         SCIPsetDebugMsgPrint(set, " Applying resolution with resolving variable <%s>\n", SCIPvarGetName(vartoresolve));
         SCIP_CALL( executeResolutionStep(conflict, set, vars, blkmem, bdchginfo, residx, validdepth, fixbounds, fixinds, &successresolution ) );

         /* if resolution was unsuccessful we can resolve clauses for the binary
          * case. This is called only if the clause fallback parameter is true */
         if (!successresolution)
         {
            resolveClauses(set, conflict, vars, bdchginfo, blkmem, residx, nvars, fixbounds, fixinds, &successresolution);
            if (!successresolution)
            {
               conflict->nknownaborts++;
               goto TERMINATE_RESOLUTION_LOOP;
            }
         }

         /* we must reset the conflict lower and upper bound to be able to add weaker bounds later */
         if( SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_LOWER )
         {
            vars[residx]->conflictreslb = SCIP_REAL_MIN;
         }
         else
         {
            assert(SCIPbdchginfoGetBoundtype(bdchginfo) == SCIP_BOUNDTYPE_UPPER);
            vars[residx]->conflictresub = SCIP_REAL_MAX;
         }

         SCIP_CALL( conflictRowReplace(conflictrow, blkmem, resolvedconflictrow) );

#ifdef SCIP_CONFGRAPH_DOT
         confgraphAddRow(conflict, RESOLVED_ROW);
#endif
         if( conflictrow->nnz > maxsize )
         {
            SCIPsetDebugMsgPrint(set, "Number of nonzeros in conflict is larger than maxsize %d > %d\n",
                           conflictrow->nnz, maxsize);
            SCIPsetDebugMsgPrint(set, "Slack of resolved row before weakening: %f \n", getSlackConflictRow(set, vars, conflictrow, bdchginfo, fixbounds, fixinds));
            weakenConflictRow(conflictrow, set, vars, bdchgidx, fixbounds, fixinds);
            if(conflictrow->nnz > maxsize)
            {
               conflict->nreslongconfs++;
               conflict->nknownaborts++;
               goto TERMINATE_RESOLUTION_LOOP;
            }
         }

         conflictrow->slack = getSlackConflictRow(set, vars, conflictrow, bdchginfo, fixbounds, fixinds);

         SCIPsetDebugMsgPrint(set, "Slack of resolved row: %f \n", conflictrow->slack);

         SCIPdebug(printConflictRow(conflict->conflictrow, set, vars, RESOLVED_CONFLICT_ROWTYPE));

         /** Unfortunately we cannot guarrante that the slack becomes zero after reducing the reason (even if we have only binary variables)
          *  Till now there are two major problems:
          *    - Knapsack constraints that use negated cliques in the propagation
          *    - Ranged row propagation (gcd argument)
          */

         /* check that we fail for a valid reason */
         if (SCIPsetIsGE(set, conflictrow->slack, 0.0))
         {
            if(isBinaryConflictRow(set, vars, conflictrow) && isBinaryConflictRow(set, vars, reasonrow))
            {
               /* we can use clausal conflict analysis as a last resort */
               if( set->conf_clausefallback )
               {
                  resolveClauses(set, conflict, vars, bdchginfo, blkmem, residx, nvars, fixbounds, fixinds, &successresolution);
                  if (!successresolution)
                  {
                     conflict->nknownaborts++;
                     goto TERMINATE_RESOLUTION_LOOP;
                  }
               }
               else if ( set->conf_reductiontechnique != 'o' )
               {
                  SCIPsetDebugMsgPrint(set, "Terminate for an unknown reason\n");
                  conflict->nunresolvable++;
                  goto TERMINATE_RESOLUTION_LOOP;
               }
               else
               {
                  conflict->nknownaborts++;
                  goto TERMINATE_RESOLUTION_LOOP;
               }
            }
            else
            {
               conflict->nknownaborts++;
               conflict->nunresolvable++;
               goto TERMINATE_RESOLUTION_LOOP;
            }
         }

         nressteps++;

         if( set->conf_weakenconflict)
            weakenConflictRow(conflictrow, set, vars, bdchgidx, fixbounds, fixinds);

         /* apply coefficient tightening to the resolved constraint should never hurt */
         SCIP_CALL( tightenCoefs(set, vars, FALSE, conflictrow->vals, conflictrow->inds,
                        &conflictrow->nnz, &conflictrow->lhs, &nchgcoefs, TRUE, NULL) );
         if (nchgcoefs > 0)
         {
            SCIP_Real newslack;
            SCIP_Real previousslack;

            /* The new slack should always be less or equal to the old slack */
            previousslack = conflictrow->slack;
            newslack = getSlackConflictRow(set, vars, conflictrow, bdchginfo, fixbounds, fixinds);
            conflictrow->slack = newslack;
            SCIPsetDebugMsgPrint(set, "Tightened %d coefficients in the resolved constraint, old slack %f, new slack %f \n", nchgcoefs, previousslack, newslack);
            assert(SCIPsetIsLE(set, newslack, previousslack + EPS) || SCIPsetIsRelLE(set, newslack, previousslack));
            SCIPdebug(printConflictRow(conflictrow, set, vars, TIGHTENED_CONFLICT_ROWTYPE));

         }

         /* if we reached this point the conflict constraint must have negative slack */
         assert(SCIPsetIsLT(set, conflictrow->slack, 0.0));

         /* TODO Apply cmir after each iteration to strengthen the conflict constraint */

         /* terminate after at most nressteps resolution iterations */
         /* By default conf_maxnumressteps is -1 and hence no limit is set on the number of resolution steps */
         if( set->conf_maxnumressteps > 0 && nressteps >= set->conf_maxnumressteps )
         {
            conflict->nknownaborts++;
            goto TERMINATE_RESOLUTION_LOOP;
         }

         SCIP_CALL( updateBdchgQueue(set, vars, conflictrow, bdchgidx) );

         /* get the next bound change */
         bdchginfo = conflictFirstCand(set, conflict, FALSE);

         /* if no bound change exists the we should stop */
         /* in case we have applied resolution steps we keep the last conflict constraint */
         if( bdchginfo == NULL )
         {
            /* this can happen only if we already have resolved and some
            and it means that we have already reached a FUIP */
            SCIPsetDebugMsgPrint(set, " reached UIP in depth %d \n", bdchgdepth);
            /* add the previous conflict in the list of conflict rows */
            conflictrow->validdepth = validdepth;
            conflictrow->conflictdepth = bdchgdepth;
            conflictrow->insertdepth = validdepth;
            conflictrow->repropdepth = MAX(0, validdepth);
            SCIP_CONFLICTROW* tmpconflictrow;
            SCIP_CALL( conflictRowCopy(&tmpconflictrow, blkmem, conflictrow) );
            SCIP_CALL( conflictInsertConflictRow(conflict, set, &tmpconflictrow) );
            goto TERMINATE_RESOLUTION_LOOP;
         }

         bdchginfo = conflictRemoveCand(set, conflict, FALSE);
         bdchgidx = SCIPbdchginfoGetIdx(bdchginfo);
         vartoresolve = SCIPbdchginfoGetVar(bdchginfo);
         bdchgdepth = SCIPbdchginfoGetDepth(bdchginfo);

         residx = SCIPvarGetProbindex(vartoresolve);

         assert(!SCIPsetIsZero(set, conflictrow->vals[residx]));

         /* get the bound change before bdchginfo */
         nextbdchginfo = conflictFirstCand(set, conflict, FALSE);

         /* check if the variable we are resolving is active */
         assert(SCIPvarIsActive(vartoresolve));

         assert(nextbdchginfo == NULL || SCIPbdchginfoGetDepth(nextbdchginfo) <= bdchgdepth);

         /* when at an UIP add the previous conflict in the list of conflict rows */
         if( (nextbdchginfo == NULL || SCIPbdchginfoGetDepth(nextbdchginfo) != bdchgdepth)
                                    && SCIPbdchginfoGetDepth(bdchginfo) != lastuipdepth )
         {
            SCIPsetDebugMsgPrint(set, " reached UIP in depth %d \n", bdchgdepth);
            /* add the previous conflict in the list of conflict rows */
            conflictrow->validdepth = validdepth;
            conflictrow->conflictdepth = bdchgdepth;
            conflictrow->insertdepth = validdepth;
            /* refactortodo this may be even deeper if the bound change is not relevant for the propagation */
            conflictrow->repropdepth = (nextbdchginfo == NULL) ? 0 : SCIPbdchginfoGetDepth(nextbdchginfo);
            SCIP_CONFLICTROW* tmpconflictrow;
            SCIP_CALL( conflictRowCopy(&tmpconflictrow, blkmem, conflictrow) );
            SCIP_CALL( conflictInsertConflictRow(conflict, set, &tmpconflictrow) );
            lastuipdepth = bdchgdepth;
            nfuips ++;
            /* stop after conf_resfuiplevels UIPs */
            if (set->conf_resfuiplevels > 0 && nfuips >= set->conf_resfuiplevels)
               goto TERMINATE_RESOLUTION_LOOP;
         }
      }
#ifdef SCIP_CONFGRAPH_DOT
   endSubgraph();
#endif
   }

  TERMINATE_RESOLUTION_LOOP:

#ifdef SCIP_CONFGRAPH_DOT
   endSubgraph();
   startSubgraph(nressteps + 1);
   if (bdchginfo != NULL)
      confgraphAddBdchg(bdchginfo, CURRENT_BOUND_CHANGE);
   if (nextbdchginfo != NULL)
      confgraphAddBdchg(nextbdchginfo, NEXT_BOUND_CHANGE);
   confgraphAddRow(conflict, CONFLICT_ROW);
   endSubgraph();
#endif

   SCIPsetDebugMsgPrint(set, "Total number of conflict rows found %d\n", conflict->nconflictrows);
   SCIPsetDebugMsgPrint(set, "Total number of FUIPS found %d\n", nfuips);

   if(conflict->nconflictrows > 0)
   {
      int nconstoadd;

      nconstoadd = (set->conf_resolutioncons > 0) ? MIN(set->conf_resolutioncons, conflict->nconflictrows) : conflict->nconflictrows;

      if (set->conf_addclauseonly)
      {
         if(bdchginfo != NULL)
            SCIP_CALL(addClauseConflict(conflict, blkmem, set, stat, transprob, origprob, tree, reopt, lp, branchcand, eventqueue, cliquetable, conflict->conflictrow, bdchginfo, fixbounds, fixinds, nconss, nconfvars));

         nconstoadd = 0;
      }
      SCIP_CALL(addConflictRows(conflict, blkmem, set, stat, transprob, origprob, tree, reopt, lp, branchcand, eventqueue, cliquetable, nconstoadd, nconss, nconfvars));
   }

   freeConflictResources(conflict, blkmem, set, cutcoefs, cutinds,fixbounds, fixinds );

   return SCIP_OKAY;
}

/** analyzes conflicting bound changes that were added with calls to SCIPconflictAddBound(), and on success,
 * creates a linear constraint that explains the infeasibility
 */
SCIP_RETCODE SCIPconflictAnalyzeResolution(
   SCIP_CONFLICT*        conflict,           /**< conflict analysis data */
   BMS_BLKMEM*           blkmem,             /**< block memory of transformed problem */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< problem statistics */
   SCIP_PROB*            transprob,          /**< transformed problem */
   SCIP_PROB*            origprob,           /**< original problem */
   SCIP_TREE*            tree,               /**< branch and bound tree */
   SCIP_REOPT*           reopt,              /**< reoptimization data structure */
   SCIP_LP*              lp,                 /**< LP data */
   SCIP_BRANCHCAND*      branchcand,         /**< branching candidate storage */
   SCIP_EVENTQUEUE*      eventqueue,         /**< event queue */
   SCIP_CLIQUETABLE*     cliquetable,        /**< clique table data structure */
   SCIP_ROW*             initialconflictrow, /**< row of constraint that detected the conflict */
   int                   validdepth,         /**< minimal depth level at which the initial conflict set is valid */
   SCIP_Bool             infeasibleLP,       /**< does the conflict originate from an infeasible LP? */
   SCIP_Bool             pseudoobj,          /**< does the conflict originate from a violated pseudo objective bound? */
   SCIP_Bool*            success             /**< pointer to store whether a conflict constraint was created, or NULL */
   )
{
   int nconss;
   int nconfvars;
   int i;

   /* check if generalized resolution conflict analysis is applicable */
   if( !SCIPconflictResolutionApplicable(set) )
      return SCIP_OKAY;

   assert(conflict != NULL);
   assert(set != NULL);
   assert(origprob != NULL);
   assert(transprob != NULL);

   if( success != NULL )
      *success = FALSE;

   SCIPsetDebugMsgPrint(set, "Starting resolution based conflict analysis after infeasible propagation in depth %d\n",
                   SCIPtreeGetCurrentDepth(tree));

   /* start timing */
   SCIPclockStart(conflict->resanalyzetime, set);

   conflict->nrescalls++;

   /* setting this to true adds bound changes only to the resolution bdchg queue */
   conflict->bdchgonlyresqueue = TRUE;

   /* create the conflict graph */
#ifdef SCIP_CONFGRAPH_DOT
   assert(confgraphfile == NULL);
   SCIP_CALL( confgraphCreate(set, conflict) );
#endif

   /* analyze the conflict set, and create a conflict constraint on success */
   SCIP_CALL( conflictAnalyzeResolution(conflict, blkmem, set, stat, transprob, origprob, tree, reopt, lp, branchcand, eventqueue,
          cliquetable, initialconflictrow, validdepth, infeasibleLP, pseudoobj, &nconss, &nconfvars) );

#ifdef SCIP_CONFGRAPH_DOT
   confgraphFree();
#endif

   conflict->nressuccess += (nconss > 0 ? 1 : 0);
   conflict->nresconfconss += nconss;
   conflict->nresconfvariables += nconfvars;

   if( success != NULL )
      *success = (nconss > 0);

   /* free all conflictrows */
   for( i = 0; i < conflict->nconflictrows; i++ )
      SCIPconflictRowFree(&conflict->conflictrows[i], blkmem);

   conflict->nconflictrows = 0;
   conflict->bdchgonlyresqueue = FALSE;
   conflict->haslargecoef = FALSE;

   /* clear the bound change queues */
   SCIPpqueueClear(conflict->resbdchgqueue);
   SCIPpqueueClear(conflict->resforcedbdchgqueue);
   SCIPpqueueClear(conflict->separatebdchgqueue);
   SCIPpqueueClear(conflict->continuousbdchgqueue);

   /* stop timing */
   SCIPclockStop(conflict->resanalyzetime, set);
   SCIPsetDebugMsgPrint(set, "resolution based conflict analysis added %d constraints \n \n", nconss);

   return SCIP_OKAY;
}
