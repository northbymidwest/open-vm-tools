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
 * @file waylandUtil.cpp --
 *
 *    See waylandUtil.h.
 */

#define G_LOG_DOMAIN "dndcp"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>

#include <glib-unix.h>
#include <wayland-client.h>

#include "waylandUtil.h"


/*
 *-----------------------------------------------------------------------------
 *
 * Wayland GSource --
 *
 *      Dispatches a Wayland connection from vmusr's GLib main loop.
 *
 *-----------------------------------------------------------------------------
 */

struct WaylandSource {
   GSource source;
   struct wl_display *display;
   gpointer fdTag;
};


static gboolean
WaylandSourcePrepare(GSource *base,
                     gint *timeout)
{
   WaylandSource *src = reinterpret_cast<WaylandSource *>(base);

   *timeout = -1;
   wl_display_dispatch_pending(src->display);
   wl_display_flush(src->display);
   return FALSE;
}


static gboolean
WaylandSourceCheck(GSource *base)
{
   WaylandSource *src = reinterpret_cast<WaylandSource *>(base);

   return g_source_query_unix_fd(base, src->fdTag) != 0;
}


static gboolean
WaylandSourceDispatch(GSource *base,
                      GSourceFunc callback,
                      gpointer data)
{
   WaylandSource *src = reinterpret_cast<WaylandSource *>(base);
   GIOCondition cond = g_source_query_unix_fd(base, src->fdTag);

   if (cond & (G_IO_ERR | G_IO_HUP)) {
      g_warning("%s: lost the Wayland connection\n", __FUNCTION__);
      return G_SOURCE_REMOVE;
   }
   if ((cond & G_IO_IN) && wl_display_dispatch(src->display) < 0) {
      g_warning("%s: wl_display_dispatch failed: %s\n", __FUNCTION__,
                strerror(errno));
      return G_SOURCE_REMOVE;
   }
   return G_SOURCE_CONTINUE;
}


static GSourceFuncs sWaylandSourceFuncs = {
   WaylandSourcePrepare,
   WaylandSourceCheck,
   WaylandSourceDispatch,
   NULL,
};


GSource *
WaylandUtil_CreateSource(struct wl_display *display)   // IN
{
   WaylandSource *src = reinterpret_cast<WaylandSource *>(
      g_source_new(&sWaylandSourceFuncs, sizeof(WaylandSource)));

   src->display = display;
   src->fdTag = g_source_add_unix_fd(&src->source,
                                     wl_display_get_fd(display),
                                     (GIOCondition)(G_IO_IN | G_IO_ERR |
                                                    G_IO_HUP));
   g_source_attach(&src->source, NULL);
   return &src->source;
}


/*
 *-----------------------------------------------------------------------------
 *
 * WaylandUtil_IsWaylandSession --
 *
 *      Whether vmusr runs in a Wayland session, unless envVar forces it.
 *
 *-----------------------------------------------------------------------------
 */

bool
WaylandUtil_IsWaylandSession(const char *envVar)   // IN
{
   const char *forced = getenv(envVar);
   const char *sessionType = getenv("XDG_SESSION_TYPE");

   if (forced != NULL && strcmp(forced, "x11") == 0) {
      g_debug("%s: X11 forced by %s\n", __FUNCTION__, envVar);
      return false;
   }
   if (forced != NULL && strcmp(forced, "wayland") == 0) {
      return true;
   }
   return sessionType != NULL && strcmp(sessionType, "wayland") == 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * WaylandTransfers --
 *
 *      Non-blocking pipe reads and writes, driven by GLib fd watches.
 *
 *-----------------------------------------------------------------------------
 */

struct WaylandTransfers::Transfer {
   WaylandTransfers *owner;
   int fd;
   guint watch;
   std::string data;
   size_t offset;
   ReadDone done;
};


void
WaylandTransfers::Read(int fd,                 // IN: taken over
                       const ReadDone &done)   // IN
{
   Transfer *t = new Transfer;

   t->owner = this;
   t->fd = fd;
   t->offset = 0;
   t->done = done;
   fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
   t->watch = g_unix_fd_add_full(G_PRIORITY_DEFAULT, fd,
                                 (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                 OnReadable, t, Free);
   mWatches.push_back(t->watch);
}


void
WaylandTransfers::Write(int fd,                    // IN: taken over
                        const std::string &data)   // IN
{
   Transfer *t = new Transfer;

   t->owner = this;
   t->fd = fd;
   t->data = data;
   t->offset = 0;
   fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
   t->watch = g_unix_fd_add_full(G_PRIORITY_DEFAULT, fd, G_IO_OUT,
                                 OnWritable, t, Free);
   mWatches.push_back(t->watch);
}


void
WaylandTransfers::CancelAll()
{
   while (!mWatches.empty()) {
      guint watch = mWatches.back();
      mWatches.pop_back();
      g_source_remove(watch);
   }
}


void
WaylandTransfers::Forget(guint watch)   // IN
{
   mWatches.erase(std::remove(mWatches.begin(), mWatches.end(), watch),
                  mWatches.end());
}


void
WaylandTransfers::Free(gpointer data)   // IN
{
   Transfer *t = static_cast<Transfer *>(data);

   close(t->fd);
   delete t;
}


gboolean
WaylandTransfers::OnReadable(gint fd,
                             GIOCondition cond,
                             gpointer data)
{
   Transfer *t = static_cast<Transfer *>(data);
   char buf[4096];
   ssize_t n;

   while ((n = read(fd, buf, sizeof buf)) > 0) {
      t->data.append(buf, n);
   }
   if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      return G_SOURCE_CONTINUE;
   }
   if (n < 0) {
      g_debug("%s: read failed: %s\n", __FUNCTION__, strerror(errno));
   }
   t->owner->Forget(t->watch);
   /* The callback may start new transfers or cancel the others. */
   t->done(t->data);
   return G_SOURCE_REMOVE;
}


gboolean
WaylandTransfers::OnWritable(gint fd,
                             GIOCondition cond,
                             gpointer data)
{
   Transfer *t = static_cast<Transfer *>(data);

   while (t->offset < t->data.size()) {
      ssize_t n = write(fd, t->data.data() + t->offset,
                        t->data.size() - t->offset);
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
         return G_SOURCE_CONTINUE;
      }
      if (n < 0) {
         g_debug("%s: write failed: %s\n", __FUNCTION__, strerror(errno));
         break;
      }
      t->offset += n;
   }
   t->owner->Forget(t->watch);
   return G_SOURCE_REMOVE;
}
