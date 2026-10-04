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
 * @file waylandUtil.h
 *
 *    Helpers shared by the native Wayland DnD and copy/paste UIs: running a
 *    Wayland connection from vmusr's GLib main loop, and moving data through
 *    the pipes that Wayland data transfers use without blocking that loop.
 */

#ifndef __WAYLAND_UTIL_H__
#define __WAYLAND_UTIL_H__

#include <string>
#include <vector>

#include <glib.h>
#include <sigc++/sigc++.h>

struct wl_display;


/*
 * Attaches a source to the default GLib main context that flushes and
 * dispatches display. Destroy it with g_source_destroy + g_source_unref
 * before disconnecting the display.
 */
GSource *WaylandUtil_CreateSource(struct wl_display *display);


/*
 * Whether this looks like a Wayland session. envVar, when set to "x11" or
 * "wayland", forces the answer.
 */
bool WaylandUtil_IsWaylandSession(const char *envVar);


/*
 * In-flight pipe transfers. Each one is driven by a GLib fd watch and owns
 * its fd. Anything still in flight is cancelled when this is destroyed, so
 * callbacks never outlive their owner.
 */

class WaylandTransfers
{
public:
   typedef sigc::slot<void, const std::string &> ReadDone;

   ~WaylandTransfers() { CancelAll(); }

   /* Read fd to EOF, then call done with everything read. */
   void Read(int fd, const ReadDone &done);
   /* Write data to fd, then close it. */
   void Write(int fd, const std::string &data);
   void CancelAll();

private:
   struct Transfer;
   static gboolean OnReadable(gint fd, GIOCondition cond, gpointer data);
   static gboolean OnWritable(gint fd, GIOCondition cond, gpointer data);
   static void Free(gpointer data);
   void Forget(guint watch);

   std::vector<guint> mWatches;
};

#endif // __WAYLAND_UTIL_H__
