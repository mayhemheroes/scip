/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*  Copyright (c) 2002-2025 Zuse Institute Berlin (ZIB)                      */
/*                                                                           */
/*  Licensed under the Apache License, Version 2.0 (the "License");          */
/*  you may not use this file except in compliance with the License.         */
/*  You may obtain a copy of the License at                                  */
/*                                                                           */
/*      http://www.apache.org/licenses/LICENSE-2.0                           */
/*                                                                           */
/*  Unless required by applicable law or agreed to in writing, software      */
/*  distributed under the License is distributed on an "AS IS" BASIS,        */
/*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. */
/*  See the License for the specific language governing permissions and      */
/*  limitations under the License.                                           */
/*                                                                           */
/*  You should have received a copy of the Apache-2.0 license                */
/*  along with SCIP; see the file LICENSE. If not visit scipopt.org.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   dual.c
 * @brief  unit tests for checking dual events
 * @author João Dionísio
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "include/scip_test.h"
#include "scip/scipdefplugins.h"
#include "scip/scip_event.h"

#define EVENTHDLR_NAME         "dualboundimproved"
#define EVENTHDLR_DESC         "event handler for catching dual bound improvements"


/** structure to store event handler specific information */
struct SCIP_EventhdlrData
{
   int ndualboundimprovements;     /**< counter for the number of dual bound improvements */
};

/** GLOBAL VARIABLES **/
static SCIP* scip_test = NULL;

/** TEST SUITES **/
static
void setup(void)
{
   cr_assert(scip_test == NULL);

   /* initialize SCIP */
   SCIP_CALL( SCIPcreate(&scip_test) );
   SCIP_CALL( SCIPincludeDefaultPlugins(scip_test) );
}

static
void teardown(void)
{
   SCIP_EVENTHDLR* eventhdlr = SCIPfindEventhdlr(scip_test, EVENTHDLR_NAME);
   cr_assert(eventhdlr != NULL);
   SCIP_EVENTHDLRDATA* eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   cr_assert(eventhdlrdata != NULL);

   /* free event handler data */
   SCIPfreeBlockMemory(scip_test, &eventhdlrdata);

   SCIP_CALL( SCIPfree(&scip_test) );

   cr_assert(scip_test == NULL);
   cr_assert(BMSgetMemoryUsed() == 0, "There is a memory leak!");
}

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitDualBoundImproved)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* notify SCIP that your event handler wants to react on the event type best solution found */
   SCIP_CALL( SCIPcatchEvent(scip, SCIP_EVENTTYPE_DUALBOUNDIMPROVED, eventhdlr, NULL, NULL) );

   return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTEXIT(eventExitDualBoundImproved)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

   /* notify SCIP that your event handler wants to drop the event type best solution found */
   SCIP_CALL( SCIPdropEvent(scip, SCIP_EVENTTYPE_DUALBOUNDIMPROVED, eventhdlr, NULL, -1) );

   return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecDualBoundImproved)
{  /*lint --e{715}*/
   SCIP_EVENTHDLRDATA* eventhdlrdata;
   SCIP_Real dualvalue;

   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(scip != NULL);
   assert(SCIPeventGetType(event) == SCIP_EVENTTYPE_DUALBOUNDIMPROVED);

   SCIPdebugMsg(scip, "exec method of event handler for dual bound improvement\n");

   dualvalue = SCIPgetDualbound(scip);

   /* print best solution value */
   SCIPinfoMessage(scip, NULL, "found new best dual bound with value <%g> in SCIP <%s>\n",
      dualvalue, SCIPgetProbName(scip) );

   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   ++eventhdlrdata->ndualboundimprovements;

   return SCIP_OKAY;
}

/** includes event handler for best solution found */
static
SCIP_RETCODE SCIPincludeEventHdlrDualBoundImproved(
   SCIP*                 scip                /* SCIP data structure */
   )
{
   assert(scip != NULL);
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   SCIP_CALL( SCIPallocBlockMemory(scip, &eventhdlrdata) );
   eventhdlrdata->ndualboundimprovements = 0;

   /* create event handler for dual bound improved */
   eventhdlr = NULL;
   SCIP_CALL( SCIPincludeEventhdlrBasic(scip, &eventhdlr, EVENTHDLR_NAME, EVENTHDLR_DESC, eventExecDualBoundImproved, eventhdlrdata) );
   assert(eventhdlr != NULL);

   SCIP_CALL( SCIPsetEventhdlrInit(scip, eventhdlr, eventInitDualBoundImproved) );
   SCIP_CALL( SCIPsetEventhdlrExit(scip, eventhdlr, eventExitDualBoundImproved) );

   return SCIP_OKAY;
}

TestSuite(events, .init = setup, .fini = teardown);

/* TESTS */

Test(events, dualboundimproved, .description = "tests SCIP_EVENTTYPE_DUALBOUNDIMPROVED generation")
{
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_EVENTHDLRDATA* eventhdlrdata;

   SCIP_CALL( SCIPincludeEventHdlrDualBoundImproved(scip_test) );

   SCIP_CALL( SCIPreadProb(scip_test, "../check/instances/MIP/bell5.mps", NULL) );

   SCIP_CALL( SCIPsolve(scip_test) );

   eventhdlr = SCIPfindEventhdlr(scip_test, EVENTHDLR_NAME);
   cr_assert(eventhdlr != NULL);
   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   cr_assert(eventhdlrdata != NULL);

   cr_expect(eventhdlrdata->ndualboundimprovements >= 1, "No dual bound improvements detected");

#ifdef SCIP_WITH_EXACTSOLVE
   SCIP_CALL( SCIPfreeProb(scip_test) );
   SCIP_CALL( SCIPenableExactSolving(scip_test, TRUE) );

   /* create the following problem with LP dual bound 1 and optimal value 1+mu with mu = 1e-15, such that the event
    * should only be triggered if an increase in the exact lowerbound is detected:
    *
    * min   x1 s.t.
    * s.t.  x1 >= 1 + mu * x2 - mu * (1 - x2)  equivalent  x1 - 2mu * x2 >= 1 + mu
    *       x1 >= 1 - mu * x2 + mu * (1 - x2)  equivalent  x1 + 2mu * x2 >= 1 + mu
    *       x1 >= 0, x2 binary
    */
   {
      SCIP_VAR* vars[2];
      SCIP_CONS* cons;
      SCIP_RATIONAL* vals[2];
      SCIP_RATIONAL* lhs;
      SCIP_RATIONAL* rhs;

      SCIPinfoMessage(scip_test, NULL, "Creating rational test problem ...\n");

      SCIP_CALL( SCIPcreateProbBasic(scip_test, "problem") );
      SCIPinfoMessage(scip_test, NULL, "... problem created\n");

      SCIP_CALL( SCIPcreateVarBasic(scip_test, &vars[0], "x0", 0.0, SCIPinfinity(scip_test), 1.0, SCIP_VARTYPE_CONTINUOUS) );
      SCIP_CALL( SCIPaddVarExactData(scip_test, vars[0], NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip_test, vars[0]) );

      SCIP_CALL( SCIPcreateVarBasic(scip_test, &vars[1], "x1", 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY) );
      SCIP_CALL( SCIPaddVarExactData(scip_test, vars[1], NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip_test, vars[1]) );
      SCIPinfoMessage(scip_test, NULL, "... variables created\n");

      SCIP_CALL( SCIPrationalCreateString(SCIPblkmem(scip_test), &vals[0], "1") );
      SCIP_CALL( SCIPrationalCreateString(SCIPblkmem(scip_test), &vals[1], "-2/1000000000000000") );
      SCIP_CALL( SCIPrationalCreateString(SCIPblkmem(scip_test), &lhs, "1000000000000001/1000000000000000") );
      SCIP_CALL( SCIPrationalCreateString(SCIPblkmem(scip_test), &rhs, "inf") );
      SCIP_CALL( SCIPcreateConsBasicExactLinear(scip_test, &cons, "c1", 2, vars, vals, lhs, rhs) );
      SCIP_CALL( SCIPaddCons(scip_test, cons) );
      SCIP_CALL( SCIPreleaseCons(scip_test, &cons) );

      SCIPrationalSetString(vals[1], "2/1000000000000000");
      SCIP_CALL( SCIPcreateConsBasicExactLinear(scip_test, &cons, "c1", 2, vars, vals, lhs, rhs) );
      SCIP_CALL( SCIPaddCons(scip_test, cons) );
      SCIP_CALL( SCIPreleaseCons(scip_test, &cons) );
      SCIPinfoMessage(scip_test, NULL, "... constraint created\n");

      SCIP_CALL( SCIPreleaseVar(scip_test, &vars[0]) );
      SCIP_CALL( SCIPreleaseVar(scip_test, &vars[1]) );

      SCIPrationalFreeBlock(SCIPblkmem(scip_test), &rhs);
      SCIPrationalFreeBlock(SCIPblkmem(scip_test), &lhs);
      SCIPrationalFreeBlock(SCIPblkmem(scip_test), &vals[1]);
      SCIPrationalFreeBlock(SCIPblkmem(scip_test), &vals[0]);
      SCIPinfoMessage(scip_test, NULL, "... data released and freed\n");
   }

   /* reset counter */
   eventhdlr = SCIPfindEventhdlr(scip_test, EVENTHDLR_NAME);
   cr_assert(eventhdlr != NULL);
   eventhdlrdata = SCIPeventhdlrGetData(eventhdlr);
   cr_assert(eventhdlrdata != NULL);
   eventhdlrdata->ndualboundimprovements = 0;

   /* solve without presolving */
   SCIP_CALL( SCIPsetIntParam(scip_test, "presolving/maxrounds", 0) );
   SCIP_CALL( SCIPsolve(scip_test) );

   /* we expect at least four updates: -SCIPinfinity() after presolve, 0 as pseudo bound, 1 as LP bound, and 1+mu */
   cr_expect(eventhdlrdata->ndualboundimprovements >= 4, "Too few dual bound improvements detected (expecting 3 updates)");
#endif
}
