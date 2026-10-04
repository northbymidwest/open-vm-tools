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
 * @file dndUIWayland.h
 *
 *    Native Wayland DnD UI for protocols V3 or greater.
 *
 *    DnDUIX11 drags through Xwayland on a Wayland session, which depends on
 *    the compositor's bridge between X11 and Wayland drags. Some compositors
 *    (KWin) don't carry those drags across reliably. This class does the
 *    same job with Wayland protocols directly: a wlr-layer-shell surface is
 *    the drag detection window, a wl_data_source started from a uinput
 *    button press is the host-to-guest drag, and the surface's
 *    wl_data_device events detect guest-to-host drags.
 *
 *    It needs the compositor to offer zwlr_layer_shell_v1, so it is only
 *    used when IsSupported() says so; otherwise DnDUIX11 is used.
 */

#ifndef __DND_UI_WAYLAND_H__
#define __DND_UI_WAYLAND_H__

#include <string>
#include <vector>

#include <glib.h>
#include <sigc++/sigc++.h>

#include "stringxx/string.hh"
#include "dnd.h"
#include "dndUI.h"

extern "C" {
#include "dndClipboard.h"
#include "vmware/tools/plugin.h"
}

#include "guestDnD.hh"

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct wl_pointer;
struct wl_output;
struct wl_surface;
struct wl_buffer;
struct wl_data_device_manager;
struct wl_data_device;
struct wl_data_source;
struct wl_data_offer;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

class DnDUIWayland
   : public DnDUI,
     public sigc::trackable
{
public:
   DnDUIWayland(ToolsAppCtx *ctx);
   ~DnDUIWayland();
   static bool IsSupported();
   bool Init();
   void VmxDnDVersionChanged(RpcChannel *chan, uint32 version);
   void SetDnDAllowed(bool isDnDAllowed)
      {ASSERT(mDnD); mDnD->SetDnDAllowed(isDnDAllowed);}
   void SetBlockControl(DnDBlockControl *blockCtrl);

   /*
    * Wayland listener entry points. Public only so the C listener
    * trampolines in dndUIWayland.cpp can reach them.
    */
   void OnRegistryGlobal(struct wl_registry *reg, uint32 name,
                         const char *iface, uint32 version);
   void OnOutputMode(uint32 flags, int32 width, int32 height);
   void OnOutputScale(int32 scale);
   void OnSeatCapabilities(uint32 caps);
   void OnPointerEnter(struct wl_surface *surface);
   void OnPointerLeave(struct wl_surface *surface);
   void OnPointerButton(uint32 serial, uint32 state);
   void OnLayerConfigure(struct zwlr_layer_surface_v1 *ls, uint32 serial);
   void OnSourceSend(struct wl_data_source *source, const char *mimeType,
                     int32 fd);
   void OnSourceAction(struct wl_data_source *source, uint32 action);
   void OnSourceEnded(struct wl_data_source *source, bool finished);
   void OnDataOffer(struct wl_data_offer *offer);
   void OnSelectionOffer(struct wl_data_offer *offer);
   void OnOfferMimeType(struct wl_data_offer *offer, const char *mimeType);
   void OnOfferSourceActions(struct wl_data_offer *offer, uint32 actions);
   void OnDeviceEnter(uint32 serial, struct wl_surface *surface,
                      struct wl_data_offer *offer);
   void OnDeviceLeave();
   void OnDeviceDrop();
   void OnReceiveDone(const std::string &mimeType, const std::string &data);

private:
   /*
    * Wayland connection and detection surface.
    */
   bool Connect();
   void Disconnect();
   bool CreateDetWnd();
   void ShowDetWnd(int32 x, int32 y);
   void HideDetWnd();
   bool WaitFor(const bool &flag, int timeoutMs);
   void Flush();

   /*
    * Fake pointer.
    */
   void FakeMove(int32 x, int32 y);
   void FakeButton(bool press);
   void UpdateFakeMouseSize();

   /*
    * Blocking FS Helper Functions.
    */
   void AddBlock();
   void RemoveBlock();

   /*
    * Callbacks from Common DnD layer.
    */
   void ResetUI();
   void OnMoveMouse(int32 x, int32 y);

   /*
    * Source functions for HG DnD.
    */
   void OnSrcDragBegin(const CPClipboard *clip, std::string stagingDir);
   void OnSrcDrop();
   void OnSrcCancel();

   /*
    * Called when GH DnD is completed.
    */
   void OnPrivateDrop(int32 x, int32 y);
   void OnDestCancel();

   /*
    * Source functions for file transfer.
    */
   void OnGetFilesDone(bool success);

   /*
    * Callbacks for showing/hiding detection window.
    */
   void OnUpdateDetWnd(bool bShow, int32 x, int32 y);
   void OnDestMoveDetWndToMousePos();

   /*
    * Source and target helpers.
    */
   void SourceDragStartDone();
   void SourceUpdateFeedback(DND_DROPEFFECT effect);
   void TargetDragEnter();
   void DestroySource();
   void DestroyOffer();
   std::string GetUriList();
   bool RequestData();
   bool SetCPClipboardFromData(const std::string &mimeType,
                               const std::string &data);
   std::string GetLastDirName(const std::string &str);

   static DND_DROPEFFECT ToDropEffect(uint32 action);
   static unsigned long GetTimeInMillis();
   static bool IsPlainText(const std::string &mimeType);
   static bool IsRichText(const std::string &mimeType);

   ToolsAppCtx *mCtx;
   GuestDnDMgr *mDnD;
   DnDBlockControl *mBlockCtrl;
   CPClipboard mClipboard;

   /* Wayland globals. */
   struct wl_display *mDisplay;
   struct wl_registry *mRegistry;
   struct wl_compositor *mCompositor;
   struct wl_shm *mShm;
   struct wl_seat *mSeat;
   struct wl_pointer *mPointer;
   struct wl_output *mOutput;
   struct wl_data_device_manager *mDataDeviceManager;
   struct wl_data_device *mDataDevice;
   struct zwlr_layer_shell_v1 *mLayerShell;
   GSource *mSource;

   /* Detection surface. */
   struct wl_surface *mSurface;
   struct zwlr_layer_surface_v1 *mLayerSurface;
   struct wl_buffer *mBuffer;
   bool mConfigured;
   bool mDetWndShown;
   int32 mDetWndX;
   int32 mDetWndY;

   /* Output size in logical pixels, for the uinput pointer's range. */
   int32 mModeWidth;
   int32 mModeHeight;
   int32 mScale;
   int32 mScreenWidth;
   int32 mScreenHeight;
   bool mUseUInput;

   /* Pointer state on the detection surface. */
   bool mPointerInDetWnd;
   bool mPressed;
   uint32 mPressSerial;

   /* HG: we are the drag source. */
   struct wl_data_source *mDataSource;
   std::vector<std::string> mSourceMimeTypes;
   std::string mHGStagingDir;
   DND_FILE_TRANSFER_STATUS mHGGetFileStatus;
   bool mBlockAdded;
   bool mInHGDrag;
   DND_DROPEFFECT mEffect;
   uint32 mSourceAction;
   uint64 mTotalFileSize;
   int32 mMousePosX;
   int32 mMousePosY;

   /* GH: a guest drag over the detection surface. */
   struct wl_data_offer *mPendingOffer;
   struct wl_data_offer *mOffer;
   std::vector<std::string> mPendingOfferMimeTypes;
   std::vector<std::string> mOfferMimeTypes;
   uint32 mPendingOfferActions;
   uint32 mOfferActions;
   std::string mOfferAcceptedMimeType;
   uint32 mOfferAction;
   bool mGHDnDInProgress;
   bool mGHDnDDataReceived;
   int mNumPendingRequest;
   unsigned long mDestDropTime;

   /* GLib watches for in-flight data transfers, removed on destruction. */
   std::vector<guint> mIoWatches;

   friend struct DnDUIWaylandTransfer;
};

#endif // __DND_UI_WAYLAND_H__
