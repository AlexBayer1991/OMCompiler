/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-2014, Open Source Modelica Consortium (OSMC),
 * c/o Linköpings universitet, Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF THE BSD NEW LICENSE OR THE
 * GPL VERSION 3 LICENSE OR THE OSMC PUBLIC LICENSE (OSMC-PL) VERSION 1.2.
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES
 * RECIPIENT'S ACCEPTANCE OF THE OSMC PUBLIC LICENSE OR THE GPL VERSION 3,
 * ACCORDING TO RECIPIENTS CHOICE.
 *
 * The OpenModelica software and the OSMC (Open Source Modelica Consortium)
 * Public License (OSMC-PL) are obtained from OSMC, either from the above
 * address, from the URLs: http://www.openmodelica.org or
 * http://www.ida.liu.se/projects/OpenModelica, and in the OpenModelica
 * distribution. GNU version 3 is obtained from:
 * http://www.gnu.org/copyleft/gpl.html. The New BSD License is obtained from:
 * http://www.opensource.org/licenses/BSD-3-Clause.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, EXCEPT AS
 * EXPRESSLY SET FORTH IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE
 * CONDITIONS OF OSMC-PL.
 *
 */

/*! \file linearSystem.c
 */

#include <math.h>

#include "omc_error.h"
#include "linearSystem.h"
#include "linearSolverLapack.h"
#include "linearSolverLis.h"
#include "simulation_info_xml.h"

/*! \fn int initializeLinearSystems(DATA *data)
 *
 *  This function allocates memory for all linear systems.
 *
 *  \param [ref] [data]
 */
int initializeLinearSystems(DATA *data)
{
  int i, nnz;
  int size;
  LINEAR_SYSTEM_DATA *linsys = data->simulationInfo.linearSystemData;

  infoStreamPrint(LOG_LS, 1, "initialize linear system solvers");

  for(i=0; i<data->modelData.nLinearSystems; ++i)
  {
    size = linsys[i].size;
    nnz = linsys[i].nnz;

    /* allocate system data */
    linsys[i].x = (double*) malloc(size*sizeof(double));
    linsys[i].b = (double*) malloc(size*sizeof(double));

    /* check if analytical jacobian is created */
    if (1 == linsys[i].method)
    {
      if(linsys[i].jacobianIndex != -1)
      {
        assertStreamPrint(data->threadData, 0 != linsys[i].analyticalJacobianColumn, "jacobian function pointer is invalid" );
      }
      if(linsys[i].initialAnalyticalJacobian(data))
      {
        linsys[i].jacobianIndex = -1;
      }
    }

    /* allocate more system data */
    linsys[i].nominal = (double*) malloc(size*sizeof(double));
    linsys[i].min = (double*) malloc(size*sizeof(double));
    linsys[i].max = (double*) malloc(size*sizeof(double));

    linsys[i].initializeStaticLSData(data, &linsys[i]);

    /* allocate solver data */
    /* the implementation of matrix A is solver-specific */
    switch(data->simulationInfo.lsMethod)
    {
    case LS_LAPACK:
      linsys[i].A = (double*) malloc(size*size*sizeof(double));
      linsys[i].setAElement = setAElementLAPACK;
      allocateLapackData(size, &linsys[i].solverData);
      break;

    case LS_LIS:
      linsys[i].setAElement = setAElementLis;
      allocateLisData(size, size, nnz, &linsys[i].solverData);
      break;

    default:
      throwStreamPrint(data->threadData, "unrecognized linear solver");
    }
  }

  messageClose(LOG_LS);
  return 0;
}

/*! \fn int updateStaticDataOfLinearSystems(DATA *data)
 *
 *  This function allocates memory for all linear systems.
 *
 *  \param [ref] [data]
 */
int updateStaticDataOfLinearSystems(DATA *data)
{
  int i, nnz;
  int size;
  LINEAR_SYSTEM_DATA *linsys = data->simulationInfo.linearSystemData;

  infoStreamPrint(LOG_LS, 1, "update static data of linear system solvers");

  for(i=0; i<data->modelData.nLinearSystems; ++i)
  {
    linsys[i].initializeStaticLSData(data, &linsys[i]);
  }

  messageClose(LOG_LS);
  return 0;
}

/*! \fn freeLinearSystems
 *
 *  This function frees memory of linear systems.
 *
 *  \param [ref] [data]
 */
int freeLinearSystems(DATA *data)
{
  int i;
  LINEAR_SYSTEM_DATA* linsys = data->simulationInfo.linearSystemData;

  infoStreamPrint(LOG_LS, 1, "free linear system solvers");

  for(i=0; i<data->modelData.nLinearSystems; ++i)
  {
    /* free system and solver data */
    free(linsys[i].x);
    free(linsys[i].b);
    free(linsys[i].nominal);
    free(linsys[i].min);
    free(linsys[i].max);

    switch(data->simulationInfo.lsMethod)
    {
    case LS_LAPACK:
      freeLapackData(&linsys[i].solverData);
      free(linsys[i].A);
      break;

    case LS_LIS:
      freeLisData(&linsys[i].solverData);
      break;

    default:
      throwStreamPrint(data->threadData, "unrecognized linear solver");
    }

    free(linsys[i].solverData);
  }

  messageClose(LOG_LS);
  return 0;
}

/*! \fn solve non-linear systems
 *
 *  \param [in]  [data]
 *         [in]  [sysNumber] index of corresponding non-linear System
 *
 *  \author wbraun
 */
int solve_linear_system(DATA *data, int sysNumber)
{
  int success;
  LINEAR_SYSTEM_DATA* linsys = data->simulationInfo.linearSystemData;

  switch(data->simulationInfo.lsMethod)
  {
  case LS_LAPACK:
    success = solveLapack(data, sysNumber);
    break;

  case LS_LIS:
    success = solveLis(data, sysNumber);
    break;

  default:
    throwStreamPrint(data->threadData, "unrecognized linear solver");
  }
  linsys[sysNumber].solved = success;

  return 0;
}

/*! \fn check_linear_solutions
 *   This function check whether some of linear systems
 *   are failed to solve. If one is failed it returns 1 otherwise 0.
 *
 *  \param [in]  [data]
 *  \param [in]  [printFailingSystems]
 *
 *  \author wbraun
 */
int check_linear_solutions(DATA *data, int printFailingSystems)
{
  LINEAR_SYSTEM_DATA* linsys = data->simulationInfo.linearSystemData;
  int i, j, retVal=0;

  for(i=0; i<data->modelData.nLinearSystems; ++i)
  {
    if(0 == linsys[i].solved)
    {
      retVal = 1;
      if(printFailingSystems && ACTIVE_WARNING_STREAM(LOG_LS))
      {
        int indexes[2] = {1, modelInfoXmlGetEquation(&data->modelData.modelDataXml, linsys->equationIndex).id};
        warningStreamPrintWithEquationIndexes(LOG_LS, 1, indexes, "linear system %d fails at t=%g", indexes[1], data->localData[0]->timeValue);
        messageClose(LOG_LS);
      }
    }
  }

  return retVal;
}

void setAElementLAPACK(int row, int col, double value, int nth, void *data)
{
  LINEAR_SYSTEM_DATA* linsys = (LINEAR_SYSTEM_DATA*) data;
  linsys->A[row + col * linsys->size] = value;
}

void setAElementLis(int row, int col, double value, int nth, void *data)
{
  LINEAR_SYSTEM_DATA* linSys = (LINEAR_SYSTEM_DATA*) data;
  DATA_LIS* sData = (DATA_LIS*) linSys->solverData;
  lis_matrix_set_value(LIS_INS_VALUE, row, col, value, sData->A);
}
