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
#pragma ident "@(#) $Id: objheur.h,v 1.16 2005/05/31 17:20:08 bzfpfend Exp $"

/**@file   objheur.h
 * @brief  C++ wrapper for primal heuristics
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __OBJHEUR_H__
#define __OBJHEUR_H__


extern "C" 
{
#include "scip/scip.h"
}


namespace scip
{

/** C++ wrapper object for primal heuristics */
class ObjHeur
{
public:
   /** name of the primal heuristic */
   const char* const scip_name_;
   
   /** description of the primal heuristic */
   const char* const scip_desc_;
   
   /** display character of primal heuristic */
   const char scip_dispchar_;

   /** default priority of the primal heuristic */
   const int scip_priority_;

   /** frequency for calling primal heuristic */
   const int scip_freq_;

   /** frequency offset for calling primal heuristic */
   const int scip_freqofs_;
   
   /** maximal depth level to call heuristic at (-1: no limit) */
   const int scip_maxdepth_;

   /** call heuristic at nodes where only a pseudo solution exist? */
   const Bool scip_pseudonodes_;

   /** call heuristic during plunging? */
   const Bool scip_duringplunging_;

   /** call heuristic after or before the current node was solved? */
   const Bool scip_afternode_;

   /** default constructor */
   ObjHeur(
      const char*   name,               /**< name of primal heuristic */
      const char*   desc,               /**< description of primal heuristic */
      char          dispchar,           /**< display character of primal heuristic */
      int           priority,           /**< priority of the primal heuristic */
      int           freq,               /**< frequency for calling primal heuristic */
      int           freqofs,            /**< frequency offset for calling primal heuristic */
      int           maxdepth,           /**< maximal depth level to call heuristic at (-1: no limit) */
      Bool          pseudonodes,        /**< call heuristic at nodes where only a pseudo solution exist? */
      Bool          duringplunging,     /**< call heuristic during plunging? */
      Bool          afternode           /**< call heuristic after or before the current node was solved? */
      )
      : scip_name_(name),
        scip_desc_(desc),
        scip_dispchar_(dispchar),
        scip_priority_(priority),
        scip_freq_(freq),
        scip_freqofs_(freqofs),
        scip_maxdepth_(maxdepth),
        scip_pseudonodes_(pseudonodes),
        scip_duringplunging_(duringplunging),
        scip_afternode_(afternode)
   {
   }

   /** destructor */
   virtual ~ObjHeur()
   {
   }

   /** destructor of primal heuristic to free user data (called when SCIP is exiting) */
   virtual RETCODE scip_free(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur                /**< the primal heuristic itself */
      )
   {
      return SCIP_OKAY;
   }
   
   /** initialization method of primal heuristic (called after problem was transformed) */
   virtual RETCODE scip_init(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur                /**< the primal heuristic itself */
      )
   {
      return SCIP_OKAY;
   }
   
   /** deinitialization method of primal heuristic (called before transformed problem is freed) */
   virtual RETCODE scip_exit(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur                /**< the primal heuristic itself */
      )
   {
      return SCIP_OKAY;
   }
   
   /** solving process initialization method of primal heuristic (called when branch and bound process is about to begin)
    *
    *  This method is called when the presolving was finished and the branch and bound process is about to begin.
    *  The primal heuristic may use this call to initialize its branch and bound specific data.
    *
    */
   virtual RETCODE scip_initsol(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur                /**< the primal heuristic itself */
      )
   {
      return SCIP_OKAY;
   }
   
   /** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed)
    *
    *  This method is called before the branch and bound process is freed.
    *  The primal heuristic should use this call to clean up its branch and bound data.
    */
   virtual RETCODE scip_exitsol(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur                /**< the primal heuristic itself */
      )
   {
      return SCIP_OKAY;
   }
   
   /** execution method of primal heuristic
    *
    *  Searches for feasible primal solutions. The method is called in the node processing loop.
    *
    *  possible return values for *result:
    *  - SCIP_FOUNDSOL   : at least one feasible primal solution was found
    *  - SCIP_DIDNOTFIND : the heuristic searched, but did not find a feasible solution
    *  - SCIP_DIDNOTRUN  : the heuristic was skipped
    *  - SCIP_DELAYED    : the heuristic was skipped, but should be called again as soon as possible, disregarding
    *                      its frequency
    */
   virtual RETCODE scip_exec(
      SCIP*         scip,               /**< SCIP data structure */
      HEUR*         heur,               /**< the primal heuristic itself */
      RESULT*       result              /**< pointer to store the result of the heuristic call */
      ) = 0;
};

} /* namespace scip */


   
/** creates the primal heuristic for the given primal heuristic object and includes it in SCIP
 *
 *  The method should be called in one of the following ways:
 *
 *   1. The user is resposible of deleting the object:
 *       CHECK_OKAY( SCIPcreate(&scip) );
 *       ...
 *       MyHeur* myheur = new MyHeur(...);
 *       CHECK_OKAY( SCIPincludeObjHeur(scip, &myheur, FALSE) );
 *       ...
 *       CHECK_OKAY( SCIPfree(&scip) );
 *       delete myheur;    // delete heur AFTER SCIPfree() !
 *
 *   2. The object pointer is passed to SCIP and deleted by SCIP in the SCIPfree() call:
 *       CHECK_OKAY( SCIPcreate(&scip) );
 *       ...
 *       CHECK_OKAY( SCIPincludeObjHeur(scip, new MyHeur(...), TRUE) );
 *       ...
 *       CHECK_OKAY( SCIPfree(&scip) );  // destructor of MyHeur is called here
 */
extern
RETCODE SCIPincludeObjHeur(
   SCIP*            scip,               /**< SCIP data structure */
   scip::ObjHeur*   objheur,            /**< primal heuristic object */
   Bool             deleteobject        /**< should the primal heuristic object be deleted when heuristic is freed? */
   );

/** returns the heur object of the given name, or NULL if not existing */
extern
scip::ObjHeur* SCIPfindObjHeur(
   SCIP*            scip,               /**< SCIP data structure */
   const char*      name                /**< name of primal heuristic */
   );

/** returns the heur object for the given primal heuristic */
extern
scip::ObjHeur* SCIPgetObjHeur(
   SCIP*            scip,               /**< SCIP data structure */
   HEUR*            heur                /**< primal heuristic */
   );

#endif
