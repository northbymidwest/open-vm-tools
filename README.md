# The `wayland-dnd` branch: native Wayland drag and drop and copy/paste

This branch adds native Wayland backends to `vmware-user`'s `dndcp` plugin.
Upstream, drag and drop and copy/paste on a Wayland session go through
Xwayland, which depends on the compositor carrying drags and the clipboard
between X11 and Wayland clients. KDE Plasma's KWin doesn't do that for
`vmware-user`: dropping files dragged in from the host never completes, and
copying from the guest to the host does nothing.

With this branch, `vmware-user` talks to the compositor directly where the
compositor supports it, and falls back to the existing X11 code everywhere
else.

## Compositor requirements

Each feature picks its backend at startup, independently:

| Feature | Wayland protocols needed | Otherwise |
|---|---|---|
| Drag and drop | `zwlr_layer_shell_v1`, `wl_data_device_manager` version 3, `wp_viewporter` | X11 (`DnDUIX11`) |
| Copy/paste | `ext_data_control_manager_v1` | X11 (`CopyPasteUIX11`) |

Drag and drop also needs the uinput file descriptor that
`vmware-user-suid-wrapper` passes as `--uinputFd`. Both features want the
`vmblock-fuse` mount, as upstream does, so that file transfers block readers
until the files have arrived.

To see what a compositor offers, run `wayland-info` (from wayland-utils) in
the session:

```sh
wayland-info | grep -E 'layer_shell|data_device_manager|viewporter|data_control'
```

Tested with VMware Fusion on an Apple silicon Mac and a NixOS guest:

| Session | Drag and drop | Copy/paste |
|---|---|---|
| KDE Plasma 6 (KWin) | native Wayland | native Wayland |
| Sway (wlroots) | native Wayland | native Wayland |
| GNOME (Mutter) | X11 fallback | X11 fallback |

Mutter offers neither layer-shell nor `ext-data-control`, but its Xwayland
bridge handles the X11 path fine. Compositors that only offer the older
`zwlr_data_control_manager_v1` also fall back to X11 for copy/paste.

## Known limitation: dragging out of the guest

Dragging from the guest to the host is unreliable with either backend, X11
included. When the pointer leaves the VM window, the host releases the
guest's mouse button right away, but its `DND_CMD_QUERY_EXITING` message
reaches `vmware-user` through the polled RPC channel, which backs off to 100ms
when idle. The guest drag is usually dropped before `vmware-user` hears about
it. Copying and pasting files works in both directions.

## Building

The Wayland backends are built by default when `wayland-client` and
`wayland-scanner` are found. The protocol XML they use is included in
`open-vm-tools/services/plugins/dndcp/wayland`. Pass `--with-wayland` to
`configure` to make them required, or `--without-wayland` to leave them out:

```sh
cd open-vm-tools
autoreconf -i
./configure --with-wayland
make
```

## NixOS

The branch is a flake. Its NixOS module, used together with NixOS's own
`virtualisation.vmware.guest`, does everything needed:

- builds your nixpkgs' `open-vm-tools` from this branch, with the Wayland
  backends, as `virtualisation.vmware.guest.package`;
- turns the desktop parts on (`headless = false`): NixOS only does that when
  `services.xserver` is enabled, which Wayland-only desktops don't need;
- starts `vmware-user` through the setuid wrapper from an XDG autostart entry:
  NixOS only starts it from X11 session commands, which Wayland sessions
  don't run.

Each of these is only a default, so any of them can still be overridden.

Add the flake to your system flake and import the module:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    open-vm-tools-wayland = {
      url = "github:northbymidwest/open-vm-tools/wayland-dnd";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { nixpkgs, open-vm-tools-wayland, ... }:
    {
      nixosConfigurations.my-vm = nixpkgs.lib.nixosSystem {
        system = "aarch64-linux"; # or "x86_64-linux"
        modules = [
          ./configuration.nix
          open-vm-tools-wayland.nixosModules.default
          { virtualisation.vmware.guest.enable = true; }
        ];
      };
    };
}
```

Then rebuild, and log out and back in so `vmware-user` starts in the new
session:

```sh
sudo nixos-rebuild switch --flake .#my-vm
```

To check that it is running, with access to vmblock and uinput:

```sh
pgrep -af 'vmtoolsd -n vmusr'
# .../open-vm-tools-13.1.0-wayland-dnd/bin/vmtoolsd -n vmusr --blockFd 3 --uinputFd 4
```

The flake also provides the package itself (`packages.<system>.default`, e.g.
`nix build github:northbymidwest/open-vm-tools/wayland-dnd`) and an overlay
(`overlays.default`) that replaces `open-vm-tools`, for setups that don't use
the module.

## Using it

Outside NixOS, `vmware-user` (`vmtoolsd -n vmusr`) has to be started inside
the Wayland session through `vmware-user-suid-wrapper`, e.g. from an XDG
autostart entry; the NixOS module above does this for you. Nothing else
changes: the plugin picks the Wayland backends
on its own when the session is Wayland (`XDG_SESSION_TYPE=wayland`) and the
compositor has the protocols above.

To override that choice, set these in `vmware-user`'s environment:

- `VMTOOLS_DND_BACKEND=x11` or `=wayland` for drag and drop
- `VMTOOLS_CP_BACKEND=x11` or `=wayland` for copy/paste

`wayland` still falls back to X11 if the compositor lacks a required protocol.

With debug logging for `vmusr` enabled in `tools.conf`, the log says which
backend was chosen: look for `native Wayland DnD` and
`native Wayland copy/paste`.

---

# General
## What is the open-vm-tools project?
open-vm-tools is a set of services and modules that enable several features in VMware products for better management of, and seamless user interactions with, guests. It includes kernel modules for enhancing the performance of virtual machines running Linux or other VMware supported Unix like guest operating systems. 
 
open-vm-tools enables the following features in VMware products:

- Graceful execution of power operations (reboot and shutdown) in the guest.
- Execution of built-in or user configured scripts in the guest during various power operations.
- Running programs, commands and file system operations in the guest to enhance guest automation.
- Authentication for guest operations.
- Generation of heartbeat from guest to host for vSphere HA solution to determine guest's availabilty.
- Clock synchronization between guest and host.
- Quiescing guest file systems to allow host to capture file-system-consistent guest snapshot.
- Execution of pre-freeze and post-thaw scripts while quiescing guest file systems.
- Customization of the guest immediately after power on.
- Periodic collection of network, disk, and memory usage information from the guest.
- Resizing the graphical desktop screen of the guest.
- Shared Folders operations between host and guest file systems on VMware Workstation and VMware Fusion.
- Copying and pasting text, graphics, and files between guest and host or client desktops.
- Dragging and dropping files between guest and host UI.
- Periodic collection of running applications, services, and containers in the guest.
- Accessing content from GuestStore.
- Publishing data to Guest Data Publisher.
- Managing Salt-Minion desired state specified in a guest variable.

## Can you provide more details on the actual code being released?
The following components have been released as open source software:
- Solaris and FreeBSD drivers for various devices and file system access.
- Linux drivers have been upstreamed to the Linux community and are not provided in the open-vm-tools release.
- The PowerOps plugin to perform graceful power operation and run power scripts.
- The VIX plugin to run programs and commands, and perform file system operations in guest.
- The GuestInfo plugin to periodically collect various statistics from guest.
- The TimeSync plugin to perform time synchronization.
- The dndcp plugin to support drag and drop, and text and file copy/paste operations.
- The ResolutionSet plugin to adjust guest screen resolutions automatically based on window sizes.
- The vmbackup plugin to support quiesced snapshot operation.
- The GuestStore plugin to support GuestStore operation.
- The gdp plugin to support guest data publishing operation.
- The AppInfo plugin to periodically collect application information.
- The ServiceDiscovery plugin to periodically collect service information.
- The ContainerInfo plugin to periodically collect container information.
- The ComponentMgr plugin to handle desired state operations.
- The guest authentication service.
- The toolbox command to perform disk wiping and shrinking, manage power scripts, and time synchronization.
- The guest SDK libraries to provide information about virtual machine to guest.
- Client and server for shared folders support.
- Multiple monitor support.
- Other utilities.
 
## Is open-vm-tools available with Linux distributions?
Yes. open-vm-tools packages for user space components are available with new versions of major Linux distributions, and are installed as part of the OS installation in several cases. Please refer to VMware KB article http://kb.vmware.com/kb/2073803 for details. All leading Linux vendors support open-vm-tools and bundle it with their products. For information about OS compatibility for open-vm-tools, see the 
VMware Compatibility Guide at http://www.vmware.com/resources/compatibility
Automatic installation of open-vm-tools along with the OS installation eliminates the need to separately install open-vm-tools in guests. If open-vm-tools is not installed automatically, you may be able to manually install it from the guest OS vendor's public repository. Installing open-vm-tools from the Linux vendor's repository reduces virtual machine downtime because future updates to open-vm-tools are included with the OS maintenance patches and updates.
**NOTE**: Most of the Linux distributions ship two or more open-vm-tools packages. "open-vm-tools" is the core package without any dependencies on X libraries and "open-vm-tools-desktop" is an additional package with dependencies on "open-vm-tools" core package and X libraries. The "open-vm-tools-sdmp" package contains a plugin for Service Discovery. There may be additional packages, please refer to the documentation of the OS vendor. Note that the open-vm-tools packages available with Linux distributions do not include Linux drivers because Linux drivers are available as part of Linux kernel itself. Linux kernel versions 3.10 and later include all of the Linux drivers present in open-vm-tools except the vmhgfs driver. The vmhgfs driver was required for enabling shared folders feature, but is superseded by vmhgfs-fuse which does not require a kernel driver.

## Will there be continued support for VMware Tools and OSP? 
VMware Tools will continue to be available under a commercial license. It is recommended that open-vm-tools be used for the Linux distributions where open-vm-tools is available. VMware will not provide OSPs for operating systems where open-vm-tools is available.

## How does this benefit other open source projects?
Under the terms of the GPL, open source community members are able to use the open-vm-tools code to develop their own applications, extend it, and contribute to the community. They can also incorporate some or all of the code into their projects, provided they comply with the terms of the GPL.

# License Related
## What license is the code being released under?
The code is being released under GPL v2 and GPL v2 compatible licenses. To be more specific, the Linux kernel modules are being released under the GPL v2, while almost all of the user level components are being released under the LGPL v2.1. The SVGA and mouse drivers have been available under the X11 license for quite some time. There are certain third party components released under BSD style licenses, to which VMware has in some cases contributed, and will continue to distribute with open-vm-tools.
 
## Why did you choose these licenses?
We chose the GPL v2 for the kernel components to be consistent with the Linux kernel's license. We chose the LGPL v2.1 for the user level components because some of the code is implemented as shared libraries and we do not wish to restrict proprietary code from linking against those libraries. For consistency, we decided to license the rest of the userlevel code under the LGPL v2.1 as well.

## What are the obligations that the license(s) impose?
Each of these licenses have different obligations.
For questions about the GPL, LGPL licenses, the Free Software Foundation's GPL FAQ page provides lots of useful information. 
For questions about the other licenses like the X11, BSD licenses, the Open Source Initiative has numerous useful resources including mailing lists. 
The Software Freedom Law Center provides legal expertise and consulting for free and open source software (FOSS) developers.

## Can I use all or part of this code in my proprietary software? Do I have to release the source code if I do?
Different open source licenses have different requirements regarding the release of source code. Since the code is being released under various open source licenses, you will need to comply with the terms of the corresponding licenses.

## Am I required to contribute back any changes I make to the code?
No, you aren't required to contribute any changes that you make back to the open-vm-tools project. However, we encourage you to do so.

## Can I use all or part of this code in another open source package?
Yes, as long as you comply with the appropriate license(s).
 
## Can I package this for my favorite operating system?
Yes! Please do. 

## Will the commercial version (VMware Tools) differ from the open source version (open-vm-tools)? If so, how?
Our goal is to work towards making the open source version as close to the commercial version as possible. However, we do currently make use of certain components licensed from third parties as well as components from other VMware products which are only available in binary form.

## If I use the code from the open-vm-tools project in my project/product, can I call my project/product VMware Tools?
No, since your project/product is not a VMware project/product.

# Building open-vm-tools
## How do I build open-vm-tools?
open-vm-tools uses the GNU Automake tool for generating Makefiles to build all sources. More information about Automake can be found here: http://www.gnu.org/software/automake/
## Project build information:
The following steps will work on most recent Linux distributions:
```
autoreconf -i
./configure
make
sudo make install
sudo ldconfig
```

### Service Discovery (sdmp) plugin
To build the optional sdmp (Service Discovery) plugin use the `--enable-servicediscovery` option to invoke the configure script:
```
./configure --enable-servicediscovery
```

### The open-vm-tools 12.0.0 release introduces an optional setup script and two plugins (one optional)

 * Salt Minion Setup
 * Component Manager plugin
 * ContainerInfo plugin (optional)

### Salt Minion Setup
The Salt support on Linux consists of a single bash script to setup Salt Minion on VMware virtual machines.  The script requires the "curl" and "awk" commands to be available on the system.

Linux providers supplying open-vm-tools packages are recommended to provide Salt Minion support in a separate optional package - "open-vm-tools-salt-minion".

To include the Salt Minion Setup in the open-vm-tools build use the `--enable-salt-minion` option when invoking the configure script.
```
./configure --enable-salt-minion
```

### Component Manager (componentMgr) plugin
The component Manager manages a preconfigured set of components available from VMware that can be made available on the Linux guest.  Currently the only component that can be managed is the Salt Minion Setup.

### ContainerInfo (containerInfo) plugin
The optional containerInfo plugin retrieves a list of the containers running on a Linux guest and publishes the list to the guest variable "**guestinfo.vmtools.containerinfo**" in JSON format.  The containerInfo plugin communicates with the containerd daemon using gRPC to retrieve the desired information.  For containers that are managed by Docker, the plugin uses libcurl to communicate with the Docker daemon and get the names of the containers.

Since this plugin requires additional build and runtime dependencies, Linux vendors are recommended to release it in a separate, optional package - "open-vm-tools-containerinfo".  This avoids unnecessary dependencies for customers not using the feature.

#### Canonical, Debian, Ubuntu Linux
| Build Dependencies | Runtime |
|:------------------------:|:----------------:|
| `libcurl4-openssl-dev` | `curl` |
| `protobuf-compiler` | `protobuf` |
| `libprotobuf-dev` | `grpc++` |
| `protobuf-compiler-grpc` |
| `libgrpc++-dev` |
| `golang-github-containerd-containerd-dev` |
| `golang-github-gogo-protobuf-dev` |

#### Fedora, Red Hat Enterprise Linux, ...
| Build Dependencies | Runtime |
|:------------------------:|:----------------:|
| `libcurl-devel` | `curl` |
| `protobuf-compiler` | `protobuf` |
| `protobuf-devel` | `grpc-cpp` |
| `grpc-plugins` |
| `grpc-devel` |
| `containerd-devel` |


#### Configuring the build for the ContainerInfo plugin
The configure script defaults to building the ContainerInfo when all the needed dependencies are available.  ContainerInfo will not be built if there are missing dependencies.  Invoke the configure script with `--enable-containerinfo=no` to explicitly inhibit building the plugin.
```
./configure --enable-containerinfo=no
```
If the configure script is given the option `--enable-containerinfo=yes` and any necessary dependency is not available, the configure script will terminate with an error.
```
./configure --enable-containerinfo=yes
```

## Getting configure options and help
If you are looking for help or additional settings for the building of this project, the following configure command will display a list of help options:
```
./configure --help
```
When using configure in the steps above it is only necessary to call ./configure once unless there was a problem after the first invocation.

# Getting Involved
## How can I get involved today?
You can get involved today in several different ways:
- Start using open-vm-tools today and give us feedback.
- Suggest feature enhancements.
- Identify and submit bugs under issues section: https://github.com/vmware/open-vm-tools/issues
- Start porting the code to other operating systems.   Here is the list of operating systems with open-vm-tools:

  * Red Hat Enterprise Linux 7.0 and later releases
  * SUSE Linux Enterprise 12 and later releases
  * Ubuntu 14.04 and later releases
  * CentOS 7 and later releases
  * Debian 7.x and later releases
  * Oracle Linux 7 and later 
  * Fedora 19 and later releases
  * openSUSE 11.x and later releases
  * Flatcar Container Linux, all releases
  * Rocky 8 and later releases
  * AlmaLinux OS 8 and later releases
 
## Will external developers be allowed to become committers to the project?
Yes. Initially, VMware engineers will be the only committers. As we roll out our development infrastructure, we will be looking to add external committers to the project as well.

## How can I submit code changes like bug fixes, patches, new features to the project?
Initially, you can submit bug fixes, patches and new features to the project development mailing list as attachments to emails or bug reports. To contribute source code, you will need to fill out a contribution agreement form as part of the submission process. We will have more details on this process shortly.

## What is the governance model for managing this as an open source project?
The feature roadmap and schedules for the open-vm-tools project will continue to be defined by VMware. Initially, VMware engineers will be the only approved committers. We will review incoming submissions for suitability for merging into the project. We will be looking to add community committers to the project based on their demonstrated contributions to the project. Finally, we also plan to set up a process for enhancement proposals, establishing sub-projects and so on.

## Will you ship code that I contribute with VMware products? If so, will I get credit for my contributions?
Contributions that are accepted into the open-vm-tools project's main source tree will likely be a part of VMware Tools. We also recognize the value of attribution and value your contributions. Consequently, we will acknowledge contributions from the community that are distributed with VMware's products.

## Do I need to sign something before making a contribution?
Yes. We have a standard contribution agreement that covers all contributions made to the project. It gives VMware and you joint copyright interests in the code you are contributing. The agreement also gives VMware flexibility with licensing and also helps avoid any copyright/licensing related issues that may arise in the future. In order for us to include your contribution in our source tree, we ask that you send us a signed copy of the agreement. You can do this in one of two ways:
Fax to +1.650.427.5003, Attn: Product & Technology Law Group
Scan and email it to oss-queries_at_vmware.com
Agreement: http://open-vm-tools.sourceforge.net/files/vca.pdf

## My version of Linux is not recognized.  How do I add my Linux name to the known list?

The open-vm-tools source contains a table mapping the guest distro name to the officially recognized short name.  __Please do not submit pull requests altering this table and associated code.__  Any changes here must be accompanied by additional changes in the VMware host.  Values that are not recognized by the VMware host will be ignored. 


Use the appropriate generic Linux designation when configuring a VM for your Linux version.  The selection available will vary by virtual hardware version being used.
- Other 5.x or later Linux (64-bit)
- Other 5.x or later Linux (32-bit)
- Other 4.x Linux (64-bit)
- Other 4.x Linux (32-bit)
- Other 3.x Linux (64-bit)
- Other 3.x Linux (32-bit)
- Other Linux (64-bit)
- Other Linux (32-bit)

# Compatibility

## What Operating Systems are supported for customization?
The [Guest OS Customization Support Matrix](http://partnerweb.vmware.com/programs/guestOS/guest-os-customization-matrix.pdf) provides details about the guest operating systems supported for customization.

## Which versions of open-vm-tools are compatible with other VMware products?

The [VMware Product Interoperability Matrix](http://partnerweb.vmware.com/comp_guide2/sim/interop_matrix.php) provides details about the compatibility of different versions of VMware Tools (includes open-vm-tools) and other VMware Products.

# Internationalization
## Which languages are supported?

Effective with open-vm-tools 13.0.0, only the following languages are supported:
- English
- French
- Japanese
- Spanish

The following languages will no longer be supported:
- Italian
- German
- Korean
- Simplified Chinese
- Traditional Chinese

# Other
## Mailing Lists
Please send an email to one of these mailing lists based on the nature of your question.
- Development related questions : open-vm-tools-devel@lists.sourceforge.net
- Miscellaneous questions: open-vm-tools-discuss@lists.sourceforge.net
- General project announcements: open-vm-tools-announce@lists.sourceforge.net

