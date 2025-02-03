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

/**@file   iisplugin.c
 * @brief  unit test for checking iis functionality of scip.c
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/scip.h"
#include "scip/scip_iisfinder.h"
#include "scip/scipdefplugins.h"

#include "include/scip_test.h"

/** GLOBAL VARIABLES **/
static SCIP* scip;

/** TEST SUITES **/
static
void setup(void)
{
   scip = NULL;
   char filename[SCIP_MAXSTRLEN];

   /* initialize SCIP */
   SCIP_CALL( SCIPcreate(&scip) );
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );
   TESTsetTestfilename(filename, __FILE__, "test_infeasible.lp");
   SCIP_CALL( SCIPreadProb(scip, filename, NULL) );
}

static
void teardown(void)
{
   SCIP_CALL( SCIPfree(&scip) );

   cr_assert_null(scip);
   cr_assert_eq(BMSgetMemoryUsed(), 0, "There is a memory leak!!");
}

TestSuite(iisplugin, .init = setup, .fini = teardown);


/* test that the IIS functionality works */
Test(iisplugin, valid)
{
   SCIP_IIS* iis;

   SCIP_CALL( SCIPsolve(scip) );
   iis = SCIPgetIIS(scip);
   /** ensure that the original problem is infeasible */
   cr_expect_eq(SCIPgetStatus(scip), SCIP_STATUS_INFEASIBLE, "got status %d, expected %d", SCIPgetStatus(scip), SCIP_STATUS_INFEASIBLE);
   /** ensure that the iis does not yet exist and is therefore invalid */
   cr_expect_eq(SCIPiisIsSubscipInfeasible(iis), FALSE, "iis is valid before doing any computations");
   SCIP_CALL( SCIPgenerateIIS(scip) );
   /** ensure that the iis exists and is therefore valid */
   cr_expect_eq(SCIPiisIsSubscipInfeasible(iis), TRUE, "iis is not valid");
}
