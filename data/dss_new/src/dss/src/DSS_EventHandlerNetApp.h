#ifndef __DSS_EVENTHANDLERNETAPP_H__
#define __DSS_EVENTHANDLERNETAPP_H__

/*====================================================

FILE:  DSS_EventHandlerNetApp.h

SERVICES:
   Provide a common base class for event handling.

=====================================================

Copyright (c) 2008 - 2010 Qualcomm Technologies, Inc.
All Rights Reserved.
Qualcomm Technologies Confidential and Proprietary

=====================================================*/
/*===========================================================================
  EDIT HISTORY FOR MODULE

  Please notice that the changes are listed in reverse chronological order.

  $Header: //source/qcom/qct/modem/datamodem/interface/dss/rel/11.03/src/DSS_EventHandlerNetApp.h#1 $
  $DateTime: 2011/06/17 12:02:33 $$Author: zhasan $

  when       who what, where, why
  ---------- --- ------------------------------------------------------------
  2010-04-18 en  History added.

===========================================================================*/

#include "DSS_NetApp.h"
#include "AEEISignal.h"
#include "AEEISignalCtl.h"
#include "DSS_EventHandler.h"
#include "DSS_WeakRefScope.h"
#include "DSS_MemoryManagement.h"

class DSSEventHandlerNetApp: public DSSEventHandler
{
public:
   // This class uses two-phase construction: the constructor is empty and the
   // actual initialization is done through the Init() function, so it can return
   // an error value.
   AEEResult Init(DSSNetApp* parentNetAppParam);
   virtual void Destructor(void) throw();

protected:
   void DispatchCB(dss_iface_ioctl_event_enum_type event, EventData* ped,
                   dss_iface_ioctl_event_info_union_type* eventInfo);

   DSSNetApp* parentNetApp;
};

#endif // __DSS_EVENTHANDLERNETAPP_H__
