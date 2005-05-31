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
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: pub_cutpool.h,v 1.6 2005/05/31 17:20:18 bzfpfend Exp $"

/**@file   pub_cutpool.h
 * @brief  public methods for storing cuts in a cut pool
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __PUB_CUTPOOL_H__
#define __PUB_CUTPOOL_H__


#include "scip/def.h"
#include "scip/type_cutpool.h"



/** get number of cuts in the cut pool */
extern
int SCIPcutpoolGetNCuts(
   CUTPOOL*         cutpool             /**< cut pool */
   );

/** get maximum number of cuts that were stored in the cut pool at the same time */
extern
int SCIPcutpoolGetMaxNCuts(
   CUTPOOL*         cutpool             /**< cut pool */
   );

/** gets time in seconds used for separating cuts from the pool */
extern
Real SCIPcutpoolGetTime(
   CUTPOOL*         cutpool             /**< cut pool */
   );

/** get number of times, the cut pool was separated */
extern
Longint SCIPcutpoolGetNCalls(
   CUTPOOL*         cutpool             /**< cut pool */
   );

/** get total number of cuts that were separated from the cut pool */
extern
Longint SCIPcutpoolGetNCutsFound(
   CUTPOOL*         cutpool             /**< cut pool */
   );



#endif
