/*=============================================================================
        Copyright 2006 - 2007 Qualcomm Technologies, Inc.
               All Rights Reserved.
            Qualcomm Technologies Confidential and Proprietary
===============================================================================

DESCRIPTION:  This file defines the class ID AEECLSID_CSignalCBFactory

=============================================================================
*/
// Copyright (c) 2006, 2007 Qualcomm Technologies, Inc.

/* Auto generated code by the Component Services IDL compiler.
 * Warning: DO NOT EDIT THIS FILE. CHANGES MAY BE LOST.
 */

#ifndef AEECSIGNALCBFACTORY_H
#define AEECSIGNALCBFACTORY_H

#if !defined(AEEINTERFACE_CPLUSPLUS)

#include "AEEStdDef.h"
#define AEECLSID_CSignalCBFactory 0x1041207

#else /* C++ */

#include "AEEStdDef.h"
const AEECLSID AEECLSID_CSignalCBFactory = 0x1041207;

#endif /* !defined(AEEINTERFACE_CPLUSPLUS) */

/*=============================================================================
   CLASS DOCUMENTATION
===============================================================================

AEECLSID_CSignalCBFactory

Description:

   AEECLSID_CSignalCBFactory identifes a signal factory for the current
   environment.  It is used to create signals that call functions in the
   current environment.

   AEECLSID_CSignalCBFactory is a per-environment singleton.  Multiple calls
   to CreateInstance() will return the same instance.

   Unlike most signal-based services, AEECLSID_CSignalCBFactory cannot be
   used safely in multi-threaded environments.  Its use is limited to
   single-threaded environments, such as applets.

Default Interface:
   ISignalCBFactory

Other Supported Interfaces:
   None

See Also:
   ISignalFactory
   ISignal
   AEECLSID_CSignalFactory

=============================================================================
*/
#endif /* #ifndef AEECSIGNALCBFACTORY_H */
