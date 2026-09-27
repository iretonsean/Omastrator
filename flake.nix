{
  # Omastrator: vector illustration for Linux, built with CMake.
  description = "Omastrator, vector illustration for Linux, made for Omarchy";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in {
      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "omastrator";
        version = "0.1.0";
        # dev.sh builds in ./build; the package starts clean.
        src = pkgs.lib.cleanSourceWith {
          src = self;
          filter = path: type: path != "${toString self}/build";
        };
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
        buildInputs = with pkgs; [ qt6.qtbase qt6.qtimageformats qt6.qtsvg fontconfig ];
        ninjaFlags = [ "omastrator" ];
        # Staged: profiles build MIME caches outside the store.
        installPhase = ''
          runHook preInstall
          DESTDIR=/ cmake --install . --prefix $out
          runHook postInstall
        '';
      };
      # The tests' shell: plugins and fonts found at run time.
      devShells.x86_64-linux.default = pkgs.mkShell {
        inputsFrom = [ self.packages.x86_64-linux.default ];
        QT_PLUGIN_PATH = "${pkgs.qt6.qtimageformats}/lib/qt-6/plugins:${pkgs.qt6.qtsvg}/lib/qt-6/plugins:${pkgs.qt6.qtbase}/lib/qt-6/plugins";
        FONTCONFIG_FILE = pkgs.makeFontsConf {
          fontDirectories = [ pkgs.dejavu_fonts ];
          # fontconfig's own rules, which NixOS keeps in /etc.
          includes = [ "${pkgs.fontconfig.out}/etc/fonts/conf.d" ];
        };
      };
    };
}
