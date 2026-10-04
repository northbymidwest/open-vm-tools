/*********************************************************
 * Copyright (c) 2026 Michael Bryniarski.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/**
 * @file dndUI.h
 *
 *    The interface CopyPasteDnDX11 uses to drive a DnD UI, implemented by
 *    DnDUIX11 (X11, including Xwayland) and DnDUIWayland (native Wayland).
 */

#ifndef __DND_UI_H__
#define __DND_UI_H__

#include "dnd.h"     /* for DnDBlockControl */

extern "C" {
#include "vmware/tools/guestrpc.h"
}

class DnDUI
{
public:
   virtual ~DnDUI() {}
   virtual bool Init() = 0;
   virtual void VmxDnDVersionChanged(RpcChannel *chan, uint32 version) = 0;
   virtual void SetDnDAllowed(bool isDnDAllowed) = 0;
   virtual void SetBlockControl(DnDBlockControl *blockCtrl) = 0;
};

#endif // __DND_UI_H__
