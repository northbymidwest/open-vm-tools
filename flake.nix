{
  description = "open-vm-tools with native Wayland drag and drop and copy/paste (wayland-dnd branch)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # nixpkgs' open-vm-tools, built from this branch with the Wayland
      # backends. Takes the caller's pkgs, so it matches the rest of their
      # system.
      mkOpenVmTools =
        pkgs:
        pkgs.open-vm-tools.overrideAttrs (old: {
          version = "${old.version}-wayland-dnd";
          src = self;
          sourceRoot = "source/open-vm-tools";
          nativeBuildInputs = old.nativeBuildInputs ++ [ pkgs.wayland-scanner ];
          buildInputs = old.buildInputs ++ [ pkgs.wayland ];
          configureFlags = old.configureFlags ++ [ "--with-wayland" ];
        });
    in
    {
      packages = forAllSystems (pkgs: rec {
        open-vm-tools = mkOpenVmTools pkgs;
        default = open-vm-tools;
      });

      overlays.default = final: prev: { open-vm-tools = mkOpenVmTools prev; };

      # Enable with virtualisation.vmware.guest.enable = true. Everything here
      # is a default, so it can still be overridden.
      nixosModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.virtualisation.vmware.guest;
        in
        {
          config = lib.mkIf cfg.enable {
            virtualisation.vmware.guest = {
              package = lib.mkDefault (mkOpenVmTools pkgs);
              # headless defaults to !services.xserver.enable, which is false
              # on Wayland-only desktops; drag and drop and copy/paste need the
              # non-headless parts (vmblock-fuse, vmware-user-suid-wrapper).
              headless = lib.mkDefault false;
            };

            # The NixOS module only starts vmware-user from X11 session
            # commands, which Wayland sessions don't run. XDG autostart works
            # in Plasma, GNOME and UWSM-managed compositors.
            environment.etc."xdg/autostart/vmware-user.desktop".text = lib.mkDefault ''
              [Desktop Entry]
              Type=Application
              Name=VMware User Agent
              Exec=/run/wrappers/bin/vmware-user-suid-wrapper
              NoDisplay=true
            '';
          };
        };
    };
}
