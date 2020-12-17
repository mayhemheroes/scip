/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2019 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cons_stpcomponents.c
 * @brief  Component constraint handler for Steiner problems
 * @author Daniel Rehfeldt
 *
 * This file checks for biconnected components and solves them separately.
 *
 *
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "cons_stpcomponents.h"
#include "scip/scip.h"
#include "scip/scipdefplugins.h"
#include "cons_stp.h"
#include "heur_tm.h"
#include "reader_stp.h"
#include "heur_local.h"
#include "heur_prune.h"
#include "heur_ascendprune.h"
#include "heur_slackprune.h"
#include "heur_lurkprune.h"
#include "heur_rec.h"
#include "pricer_stp.h"
#include "probdata_stp.h"
#include "dialog_stp.h"
#include "prop_stp.h"
#include "branch_stp.h"
#include "solhistory.h"
#include "graph.h"
#include "solstp.h"

/**@name Constraint handler properties
 *
 * @{
 */

#define CONSHDLR_NAME          "stpcomponents"
#define CONSHDLR_DESC          "steiner tree components constraint handler"
#define CONSHDLR_SEPAPRIORITY   9999999 /**< priority of the constraint handler for separation */
#define CONSHDLR_ENFOPRIORITY         0 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY  9999999 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_SEPAFREQ             1 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_PROPFREQ             0 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_EAGERFREQ            1 /**< frequency for using all instead of only the useful constraints in separation,
                                         *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_NEEDSCONS        FALSE /**< should the constraint handler be skipped, if no constraints are available? */


#define CONSHDLR_PROP_TIMING   SCIP_PROPTIMING_BEFORELP // SCIP_PROPTIMING_DURINGLPLOOP //  SCIP_PROPTIMING_BEFORELP



/**@} */

/*
 * Data structures
 */

/** @brief Constraint data for  \ref cons_stp.c "Stp" constraints */
struct SCIP_ConsData
{
   GRAPH*                graph;              /**< graph data structure */
};


/** @brief Constraint handler data for \ref cons_stp.c "Stp" constraint handler */
struct SCIP_ConshdlrData
{
   int                   maxsepacutsroot;    /**< maximal number of cuts separated per separation round in the root node */
};


/** representation of a biconnected component */
typedef struct sub_component
{
   int*                  subedgesToOrg;      /**< map */
   int*                  subedgesSol;        /**< solution: UNKNOWN/CONNECT */
   int                   subroot;            /**< root for orientation of solution */
   int                   nsubedges;          /**< number of edges */
   int                   nsubnodes;          /**< number of nodes */
} SUBCOMP;


/*
 * Local methods
 */



/** gets optimal sub-problem solution
 *  We mark the subedgesSol array, but for each with both orientations */
static
SCIP_RETCODE subsolGetNonOriented(
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP*              subcomp             /**< component */
   )
{

   SOLHISTORY* solhistory;
   SCIP_SOL* subsol = SCIPgetBestSol(subscip);
   GRAPH* subgraph = SCIPprobdataGetGraph2(subscip);
   const STP_Bool* edges_isInSol;
   int* subedgesSol = subcomp->subedgesSol;
   const int subroot = subcomp->subroot;
   const int nsubedges = subcomp->nsubedges;

   assert(subgraph && subsol);

   SCIP_CALL( solhistory_init(subscip, subgraph, &solhistory) );
   SCIP_CALL( solhistory_computeHistory(subscip, subsol, subgraph, solhistory) );

   assert(nsubedges == solhistory->norgedges);
   edges_isInSol = solhistory->orgedges_isInSol;

   for( int i = 0; i < nsubedges; i++ )
   {
      if( edges_isInSol[i] )
      {
         subedgesSol[i] = CONNECT;
      }
      else
      {
         subedgesSol[i] = UNKNOWN;
      }
   }

   // maybe not necessary??? DFS

   // mark solution...


   solhistory_free(subscip, &solhistory);


   return SCIP_OKAY;
}



/** orients sub-problem solution */
static
SCIP_RETCODE subsolOrient(
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP*              subcomp             /**< component */
   )
{
   // DFS

   return SCIP_OKAY;
}


/** fixes original edges */
static
SCIP_RETCODE subsolFixOrgEdges(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP*              subcomp             /**< component */
   )
{
   SCIP_VAR** orgvars = SCIPprobdataGetVars(scip);
   GRAPH* orggraph = SCIPprobdataGetGraph2(scip);
   GRAPH* subgraph = SCIPprobdataGetGraph2(subscip);
   const int* const subedgesSol = subcomp->subedgesSol;
   const int* const subedgesToOrg = subcomp->subedgesToOrg;
   const int nsubedges = subcomp->nsubedges;
   SCIP_Bool success;

   assert(orggraph && subgraph);
   assert(orgvars);
   assert(subedgesSol && subedgesToOrg);
   assert(nsubedges > 0);
   // todo
   assert(nsubedges == orggraph->edges);
   assert(solstp_isValid(scip, orggraph, subedgesSol));

   for( int i = 0; i < nsubedges; i++ )
   {
      const int orgedge = subedgesToOrg[i];
      assert(graph_edge_isInRange(orggraph, orgedge));

      if( subedgesSol[i] == CONNECT )
      {
         SCIP_CALL( SCIPStpFixEdgeVarTo1(scip, orgvars[orgedge], &success) );
      }
      else
      {
         assert(subedgesSol[i] == UNKNOWN);
         SCIP_CALL( SCIPStpFixEdgeVarTo0(scip, orgvars[orgedge], &success) );
      }
   }

   return SCIP_OKAY;
}


/** initializes */
static
SCIP_RETCODE subcompInit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP**             subcomponent        /**< to initialize */
   )
{
   SUBCOMP* subcomp;
   const GRAPH* const subgraph = SCIPprobdataGetGraph2(subscip);
   const int nsubnodes = graph_get_nNodes(subgraph);
   const int nsubedges = graph_get_nEdges(subgraph);

   assert(scip && subscip);
   assert(nsubnodes > 0);
   assert(nsubedges > 0);

   SCIP_CALL( SCIPallocMemory(subscip, subcomponent) );
   subcomp = *subcomponent;

   SCIP_CALL( SCIPallocMemoryArray(subscip, &(subcomp->subedgesToOrg), nsubedges) );
   SCIP_CALL( SCIPallocMemoryArray(subscip, &(subcomp->subedgesSol), nsubedges) );

   subcomp->nsubnodes = nsubnodes;
   subcomp->nsubedges = nsubedges;

   // todo
   subcomp->subroot = subgraph->source;

   // todo
   for( int i = 0; i < nsubedges; i++ )
      subcomp->subedgesToOrg[i] = i;

   return SCIP_OKAY;
}


/** fixes original edges with sub-solution */
static
SCIP_RETCODE subcompFixOrgEdges(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP*              subcomp             /**< component */
   )
{
   assert(scip && subscip && subcomp);

   SCIP_CALL( subsolGetNonOriented(subscip, subcomp) );
   SCIP_CALL( subsolOrient(subscip, subcomp) );
   SCIP_CALL( subsolFixOrgEdges(scip, subscip, subcomp) );

   return SCIP_OKAY;
}


/** exits */
static
void subcompFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip,            /**< sub-SCIP data structure */
   SUBCOMP**             subcomponent        /**< to initialize */
   )
{
   SUBCOMP* subcomp;

   assert(scip && subcomponent);
   subcomp = *subcomponent;
   assert(subcomp);

   SCIPfreeMemoryArray(subscip, &(subcomp->subedgesSol));
   SCIPfreeMemoryArray(subscip, &(subcomp->subedgesToOrg));
   SCIPfreeMemory(subscip, subcomponent);
}


/** helper */
static
SCIP_RETCODE subScipSetupCallbacks(
   SCIP*                 subscip             /**< sub-SCIP data structure */
   )
{
   SCIP_CALL( SCIPincludePricerStp(subscip) );

   SCIP_CALL( SCIPincludeDefaultPlugins(subscip) );

   SCIP_CALL( SCIPincludeReaderStp(subscip) );

   SCIP_CALL( SCIPincludeDialogStp(subscip) );

   SCIP_CALL( SCIPincludeConshdlrStp(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurTM(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurLocal(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurRec(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurPrune(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurAscendPrune(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurSlackPrune(subscip) );

   SCIP_CALL( SCIPStpIncludeHeurLurkPrune(subscip) );

   SCIP_CALL( SCIPincludeBranchruleStp(subscip) );

   SCIP_CALL( SCIPincludePropStp(subscip) );

   return SCIP_OKAY;
}


/** helper */
static
SCIP_RETCODE subScipSetupParameters(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip             /**< sub-SCIP data structure */
   )
{
   /* set hard-coded default parameters */
   SCIP_CALL( SCIPprobdataSetDefaultParams(subscip) );

   /* do not abort subproblem on CTRL-C */
   SCIP_CALL( SCIPsetBoolParam(subscip, "misc/catchctrlc", FALSE) );

   /* disable output to console */
   SCIP_CALL( SCIPsetIntParam(subscip, "display/verblevel", 0) );

   /* disable statistic timing inside sub SCIP */
   SCIP_CALL( SCIPsetBoolParam(subscip, "timing/statistictiming", FALSE) );

   /* forbid recursive call of heuristics and separators solving subMIPs */
   SCIP_CALL( SCIPsetSubscipsOff(subscip, TRUE) );

   /* disable expensive resolving */
   // SCIP_CALL( SCIPsetPresolving(subscip, SCIP_PARAMSETTING_FAST, TRUE) );

   /* disable STP presolving */
   SCIP_CALL( SCIPsetIntParam(subscip, "stp/reduction", 0) );

   // todo!
  // SCIP_CALL( SCIPsetRealParam(subscip, "limits/time", solvelimits->timelimit) );

   return SCIP_OKAY;
}


/** sets up the sub-SCIP */
static
SCIP_RETCODE subScipSetup(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP*                 subscip             /**< sub-SCIP data structure */
   )
{
   SCIP_CALL( subScipSetupCallbacks(subscip) );

   SCIP_CALL( subScipSetupParameters(scip, subscip) );

   return SCIP_OKAY;
}




/*
 * Callbacks
 */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyStpcomponents)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludeConshdlrStpcomponents(scip) );

   *valid = TRUE;

   return SCIP_OKAY;
}

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeStpcomponents)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* free constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIPfreeMemory(scip, &conshdlrdata);

   SCIPconshdlrSetData(conshdlr, NULL);

   return SCIP_OKAY;
}

/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolStpcomponents)
{  /*lint --e{715}*/


   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXITSOL(consExitsolStpcomponents)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata);

   return SCIP_OKAY;
}

/** frees specific constraint data */
static
SCIP_DECL_CONSDELETE(consDeleteStpcomponents)
{  /*lint --e{715}*/
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(consdata != NULL);
   assert(*consdata != NULL);

   SCIPfreeBlockMemory(scip, consdata);

   return SCIP_OKAY;
}

/** transforms constraint data into data belonging to the transformed problem */
static
SCIP_DECL_CONSTRANS(consTransStpcomponents)
{  /*lint --e{715}*/
   SCIP_CONSDATA* sourcedata;
   SCIP_CONSDATA* targetdata;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(SCIPgetStage(scip) == SCIP_STAGE_TRANSFORMING);
   assert(sourcecons != NULL);
   assert(targetcons != NULL);

   sourcedata = SCIPconsGetData(sourcecons);
   assert(sourcedata != NULL);

   /* create constraint data for target constraint */
   SCIP_CALL( SCIPallocBlockMemory(scip, &targetdata) );

   targetdata->graph = sourcedata->graph;

   /* create target constraint */
   SCIP_CALL( SCIPcreateCons(scip, targetcons, SCIPconsGetName(sourcecons), conshdlr, targetdata,
         SCIPconsIsInitial(sourcecons), SCIPconsIsSeparated(sourcecons), SCIPconsIsEnforced(sourcecons),
         SCIPconsIsChecked(sourcecons), SCIPconsIsPropagated(sourcecons),
         SCIPconsIsLocal(sourcecons), SCIPconsIsModifiable(sourcecons),
         SCIPconsIsDynamic(sourcecons), SCIPconsIsRemovable(sourcecons), SCIPconsIsStickingAtNode(sourcecons)) );

   return SCIP_OKAY;
}

/** LP initialization method of constraint handler (called before the initial LP relaxation at a node is solved) */
static
SCIP_DECL_CONSINITLP(consInitlpStpcomponents)
{  /*lint --e{715}*/

   printf("INIT LP \n");


   return SCIP_OKAY;
}

/** domain propagation method of constraint handler */
static
SCIP_DECL_CONSPROP(consPropStpcomponents)
{  /*lint --e{715}*/
   SCIP* subscip;
   GRAPH* graph;

   graph = SCIPprobdataGetGraph2(scip);
   assert(graph);

   printf("PROPAGATE \n");

   *result = SCIP_DIDNOTRUN;

   if( SCIPisStopped(scip) )
      return SCIP_OKAY;

   if( graph->terms == 1 )
      return SCIP_OKAY;

   // get current graph, with fixings etc! get from prop_stp!

   // check for biconnected components

   // if promising:

   // like decompse in reduce_sepa!
   {
      SUBCOMP* subcomp;
      GRAPH* orggraph;
      GRAPH* subgraph;

      orggraph = SCIPprobdataGetGraph2(scip);
      SCIP_CALL( graph_copy(scip, orggraph, &subgraph) );
      subgraph->is_packed = FALSE;

      SCIP_CALL( SCIPcreate(&subscip) );
      SCIP_CALL( subScipSetup(scip, subscip) );
      SCIP_CALL( SCIPprobdataCreateFromGraph(subscip, 0.0, "subproblem", subgraph) );
      SCIP_CALL( subcompInit(scip, subscip, &subcomp) );
      SCIP_CALL( SCIPsolve(subscip) );

      // check solving status!

      if( 1 )
      {
         // fix the arcs of the solution!

         // DFS from terminal...then transform back with ancestor mapping
         SCIP_CALL( subcompFixOrgEdges(scip, subscip, subcomp) );

      }

      if( SCIPgetNSols(subscip) > 0 )
      {
         SCIP_SOL*  bestsol = SCIPgetBestSol(subscip);
         printf("number of sub-problem solutions=%d \n", SCIPgetNSols(subscip));
         printf("best obj=%f \n",   SCIPgetSolOrigObj(subscip, bestsol) + SCIPprobdataGetOffset(scip));


      }
      *result = SCIP_REDUCEDDOM;


      subcompFree(scip, subscip, &subcomp);
   }


   SCIP_CALL( SCIPfree(&subscip) );


//assert(0);

   *result = SCIP_DIDNOTFIND;

   return SCIP_OKAY;
}

/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockStpcomponents)
{  /*lint --e{715}*/
   return SCIP_OKAY;
}


#define consEnfolpStpcomponents NULL
#define consEnfopsStpcomponents NULL
#define consCheckStpcomponents NULL

/*
 * Interface methods
 */

/** creates the handler for stp constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrStpcomponents(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSHDLR* conshdlr;

   /* create stp constraint handler data */
   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );

   conshdlr = NULL;
   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlrBasic(scip, &conshdlr, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY, CONSHDLR_EAGERFREQ, CONSHDLR_NEEDSCONS,
         consEnfolpStpcomponents, consEnfopsStpcomponents, consCheckStpcomponents, consLockStpcomponents,
         conshdlrdata) );
   assert(conshdlr != NULL);

   SCIP_CALL( SCIPsetConshdlrCopy(scip, conshdlr, conshdlrCopyStpcomponents, NULL) );
   SCIP_CALL( SCIPsetConshdlrDelete(scip, conshdlr, consDeleteStpcomponents) );
   SCIP_CALL( SCIPsetConshdlrTrans(scip, conshdlr, consTransStpcomponents) );
   SCIP_CALL( SCIPsetConshdlrInitsol(scip, conshdlr, consInitsolStpcomponents) );
   SCIP_CALL( SCIPsetConshdlrExitsol(scip, conshdlr, consExitsolStpcomponents) );

   SCIP_CALL( SCIPsetConshdlrInitlp(scip, conshdlr, consInitlpStpcomponents) );
   SCIP_CALL( SCIPsetConshdlrProp(scip, conshdlr, consPropStpcomponents, CONSHDLR_PROPFREQ, CONSHDLR_DELAYPROP,
         CONSHDLR_PROP_TIMING) );
   SCIP_CALL( SCIPsetConshdlrFree(scip, conshdlr, consFreeStpcomponents) );

   return SCIP_OKAY;
}
