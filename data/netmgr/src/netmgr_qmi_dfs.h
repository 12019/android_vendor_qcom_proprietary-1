/******************************************************************************

                       N E T M G R _ Q M I _D F S . H

******************************************************************************/

/******************************************************************************

  @file    netmgr_qmi_dfs.h
  @brief   Network Manager QMI data filter service header

  DESCRIPTION
  Network Manager QMI data filter service header

******************************************************************************/
/*===========================================================================

  Copyright (c) 2014 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

===========================================================================*/

/*===========================================================================
                              INCLUDE FILES
===========================================================================*/
#ifndef __NETMGR_QMI_DFS_H__
#define __NETMGR_QMI_DFS_H__
#include "qmi_client.h"
#include "qmi.h"

#include "data_filter_service_v01.h"

/*===========================================================================
  FUNCTION  netmgr_qmi_dfs_init_qmi_client
===========================================================================*/
/*!
@brief
  Initialize QMI DFS handle

@return
  int - NETMGR_QMI_DFS_SUCCESS on successful operation,
        NETMGR_QMI_DFS_FAILURE if there was an error sending QMI message

*/
/*=========================================================================*/
int netmgr_qmi_dfs_init_qmi_client
(
  int                      link,
  const char               *dev_str,
  qmi_ip_family_pref_type  ip_family,
  qmi_client_type          *user_handle
);

/*===========================================================================
  FUNCTION  netmgr_qmi_dfs_query_and_process_rev_ip_filters
===========================================================================*/
/*!
@brief
  Query and process reverse IP transport filters

@return
  int - NETMGR_QMI_DFS_SUCCESS on successful operation,
        NETMGR_QMI_DFS_FAILURE if there was an error sending QMI message

*/
/*=========================================================================*/
int netmgr_qmi_dfs_query_and_process_rev_ip_filters
(
  int                link,
  qmi_client_type    user_handle
);

/*===========================================================================
  FUNCTION  netmgr_qmi_dfs_process_ind
===========================================================================*/
/*!
@brief
 Performs processing of an incoming DFS Indication message. This function
 is executed in the Command Thread context.

@return
  void

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
void
netmgr_qmi_dfs_process_ind
(
  int           link,
  unsigned int  ind_id,
  void          *ind_data
);

#endif /* __NETMGR_QMI_DFS_H__ */
