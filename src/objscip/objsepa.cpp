/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2005 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2005 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License.             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: objsepa.cpp,v 1.11 2005/05/31 17:20:09 bzfpfend Exp $"

/**@file   objsepa.cpp
 * @brief  C++ wrapper for cut separators
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <cassert>

#include "objsepa.h"




/*
 * Data structures
 */

/** cut separator data */
struct SepaData
{
   scip::ObjSepa*   objsepa;            /**< cut separator object */
   Bool             deleteobject;       /**< should the cut separator object be deleted when cut separator is freed? */
};




/*
 * Callback methods of cut separator
 */

/** destructor of cut separator to free user data (called when SCIP is exiting) */
static
DECL_SEPAFREE(sepaFreeObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_free(scip, sepa) );

   /* free sepa object */
   if( sepadata->deleteobject )
      delete sepadata->objsepa;

   /* free sepa data */
   delete sepadata;
   SCIPsepaSetData(sepa, NULL);
   
   return SCIP_OKAY;
}


/** initialization method of cut separator (called after problem was transformed) */
static
DECL_SEPAINIT(sepaInitObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_init(scip, sepa) );

   return SCIP_OKAY;
}


/** deinitialization method of cut separator (called before transformed problem is freed) */
static
DECL_SEPAEXIT(sepaExitObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_exit(scip, sepa) );

   return SCIP_OKAY;
}


/** solving process initialization method of separator (called when branch and bound process is about to begin) */
static
DECL_SEPAINITSOL(sepaInitsolObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_initsol(scip, sepa) );

   return SCIP_OKAY;
}


/** solving process deinitialization method of separator (called before branch and bound process data is freed) */
static
DECL_SEPAEXITSOL(sepaExitsolObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_exitsol(scip, sepa) );

   return SCIP_OKAY;
}


/** execution method of separator */
static
DECL_SEPAEXEC(sepaExecObj)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->objsepa != NULL);

   /* call virtual method of sepa object */
   CHECK_OKAY( sepadata->objsepa->scip_exec(scip, sepa, result) );

   return SCIP_OKAY;
}




/*
 * cut separator specific interface methods
 */

/** creates the cut separator for the given cut separator object and includes it in SCIP */
RETCODE SCIPincludeObjSepa(
   SCIP*            scip,               /**< SCIP data structure */
   scip::ObjSepa*   objsepa,            /**< cut separator object */
   Bool             deleteobject        /**< should the cut separator object be deleted when cut separator is freed? */
   )
{
   SEPADATA* sepadata;

   /* create cut separator data */
   sepadata = new SEPADATA;
   sepadata->objsepa = objsepa;
   sepadata->deleteobject = deleteobject;

   /* include cut separator */
   CHECK_OKAY( SCIPincludeSepa(scip, objsepa->scip_name_, objsepa->scip_desc_, 
         objsepa->scip_priority_, objsepa->scip_freq_, objsepa->scip_delay_,
         sepaFreeObj, sepaInitObj, sepaExitObj, sepaInitsolObj, sepaExitsolObj, sepaExecObj,
         sepadata) );

   return SCIP_OKAY;
}

/** returns the sepa object of the given name, or NULL if not existing */
scip::ObjSepa* SCIPfindObjSepa(
   SCIP*            scip,               /**< SCIP data structure */
   const char*      name                /**< name of cut separator */
   )
{
   SEPA* sepa;
   SEPADATA* sepadata;

   sepa = SCIPfindSepa(scip, name);
   if( sepa == NULL )
      return NULL;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   return sepadata->objsepa;
}
   
/** returns the sepa object for the given cut separator */
scip::ObjSepa* SCIPgetObjSepa(
   SCIP*            scip,               /**< SCIP data structure */
   SEPA*            sepa                /**< cut separator */
   )
{
   SEPADATA* sepadata;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   return sepadata->objsepa;
}
