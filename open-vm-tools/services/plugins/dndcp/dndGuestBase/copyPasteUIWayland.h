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
 * @file copyPasteUIWayland.h
 *
 *    Native Wayland copy/paste UI for protocols V3 or greater.
 *
 *    CopyPasteUIX11 uses the X11 clipboard through Xwayland on a Wayland
 *    session, which depends on the compositor syncing the Wayland clipboard
 *    to X11. KWin only does that while an X11 window has focus, which
 *    vmware-user never has, so guest-to-host copy fails. This class uses the
 *    ext-data-control-v1 protocol instead, which lets a client without any
 *    window read and set the clipboard.
 *
 *    It needs the compositor to offer ext_data_control_manager_v1, so it is
 *    only used when IsSupported() says so; otherwise CopyPasteUIX11 is used.
 */

#ifndef __COPYPASTE_UI_WAYLAND_H__
#define __COPYPASTE_UI_WAYLAND_H__

#include <pthread.h>

#include <string>
#include <utility>
#include <vector>

#include <glib.h>
#include <sigc++/sigc++.h>

#include "stringxx/string.hh"
#include "dnd.h"
#include "copyPasteUI.h"
#include "waylandUtil.h"

extern "C" {
#include "dndClipboard.h"
}

#include "guestCopyPaste.hh"

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_source_v1;
struct ext_data_control_offer_v1;

class CopyPasteUIWayland
   : public CopyPasteUI,
     public sigc::trackable
{
public:
   CopyPasteUIWayland();
   ~CopyPasteUIWayland();
   static bool IsSupported();
   bool Init();
   void VmxCopyPasteVersionChanged(RpcChannel *chan, uint32 version);
   void SetCopyPasteAllowed(bool isCopyPasteAllowed)
      { mCP->SetCopyPasteAllowed(isCopyPasteAllowed); }
   void SetBlockControl(DnDBlockControl *blockCtrl)
      { mBlockCtrl = blockCtrl; }

   /*
    * Wayland listener entry points. Public only so the C listener
    * trampolines in copyPasteUIWayland.cpp can reach them.
    */
   void OnRegistryGlobal(struct wl_registry *reg, uint32 name,
                         const char *iface, uint32 version);
   void OnDataOffer(struct ext_data_control_offer_v1 *offer);
   void OnOfferMimeType(struct ext_data_control_offer_v1 *offer,
                        const char *mimeType);
   void OnSelection(struct ext_data_control_offer_v1 *offer);
   void OnPrimarySelection(struct ext_data_control_offer_v1 *offer);
   void OnDeviceFinished();
   void OnSourceSend(struct ext_data_control_source_v1 *source,
                     const char *mimeType, int32 fd);
   void OnSourceCancelled(struct ext_data_control_source_v1 *source);

private:
   bool Connect();
   void Disconnect();

   /* hg */
   void GetRemoteClipboardCB(const CPClipboard *clip);
   void SetSelection(const std::vector<std::string> &mimeTypes);
   void DestroySource();
   void ResetFileTransfer();
   bool StartFileTransfer();
   std::string GetFileList(const std::string &mimeType);
   void GetLocalFilesDone(bool success);
   void RemoveBlock();
   void RequestFiles();

   /* gh */
   void GetLocalClipboard();
   void OnLocalDataRead(const std::string &data, std::string mimeType,
                        uint64 request);
   void AddLocalData(const std::string &mimeType, const std::string &data);
   void SendLocalClipboard();
   void SendClipNotChanged();

   /* vmblock access monitoring, as in CopyPasteUIX11. */
   static void *FileBlockMonitorThread(void *arg);
   static gboolean RequestFilesCB(gpointer data);
   void TerminateThread();

   std::string GetLastDirName(const std::string &str);
   std::string OwnerMimeType();
   static bool IsPlainText(const std::string &mimeType);
   static bool IsRichText(const std::string &mimeType);
   static bool IsFileList(const std::string &mimeType);

   GuestCopyPasteMgr *mCP;
   DnDBlockControl *mBlockCtrl;
   bool mInited;

   /* Wayland. */
   struct wl_display *mDisplay;
   struct wl_registry *mRegistry;
   struct wl_seat *mSeat;
   struct ext_data_control_manager_v1 *mManager;
   struct ext_data_control_device_v1 *mDevice;
   GSource *mSource;
   WaylandTransfers mTransfers;

   /*
    * The current selection. An offer and its MIME types arrive before the
    * selection (or primary_selection) event that says what it is for.
    */
   struct ext_data_control_offer_v1 *mPendingOffer;
   std::vector<std::string> mPendingOfferMimeTypes;
   struct ext_data_control_offer_v1 *mSelection;
   std::vector<std::string> mSelectionMimeTypes;
   /* Bumped whenever another client sets the selection. */
   uint64 mSelectionSerial;

   /* hg: we own the selection with the host's clipboard. */
   struct ext_data_control_source_v1 *mDataSource;
   bool mIsClipboardOwner;
   std::string mHGTextData;
   std::string mHGRTFData;
   std::string mHGPNGData;
   /* NUL-delimited relative paths of the host's copied files. */
   std::string mHGFCPData;
   uint64 mTotalFileSize;
   std::string mHGStagingDir;
   DND_FILE_TRANSFER_STATUS mHGGetFileStatus;
   bool mBlockAdded;
   bool mFilesRequested;
   /* File list requests waiting for the transfer, without vmblock. */
   std::vector<std::pair<int, std::string> > mPendingFileWrites;

   /* gh: an in-flight read of the guest clipboard for the host. */
   CPClipboard mClipboard;
   uint64 mGHRequest;
   int mGHPendingReads;
   uint64 mGHRequestSerial;
   /* Selection serial last sent to the host. */
   uint64 mSentSerial;

   /* vmblock access monitoring. */
   pthread_t mThread;
   pthread_mutex_t mFileBlockMutex;
   pthread_cond_t mFileBlockCond;
   bool mFileBlockCondExit;
   std::string mFileBlockName;
   guint mRequestFilesIdle;
};

#endif // __COPYPASTE_UI_WAYLAND_H__
