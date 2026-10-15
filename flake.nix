{
  description = "A GTK 4 and libadwaita Figma client with Chromium";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux"; # The pinned CEF distribution targets Linux x86_64.
      pkgs = import nixpkgs { inherit system; };
      lib = pkgs.lib;
      uiLibraries = with pkgs; [ gtk4 libadwaita json-glib libepoxy fontconfig freetype libsodium libsecret libsoup_3 ];
      chromiumLibraries = with pkgs; [
        stdenv.cc.cc.lib glib nss nspr dbus at-spi2-core systemd
        cups libdrm libxkbcommon libGL libgbm expat alsa-lib pango cairo
        libX11 libXcomposite libXdamage libXext libXfixes libXrandr libxcb
      ];
      buildTools = with pkgs; [ cmake ninja pkg-config sysprof nodejs ];
      cefSdk = pkgs.stdenv.mkDerivation {
        pname = "figma-gtk-cef-sdk";
        version = "150.0.22";
        src = pkgs.fetchurl {
          url = "https://cef-builds.spotifycdn.com/cef_binary_150.0.22+gc1a097e+chromium-150.0.7871.256_linux64_minimal.tar.bz2";
          hash = "sha256-3Rtcw6xu/mWex+jpsGd7pOQyFqL+/K/Vq8k0CDfjLZY=";
        };
        nativeBuildInputs = [ pkgs.autoPatchelfHook ];
        buildInputs = chromiumLibraries;
        dontConfigure = true;
        dontBuild = true;
        installPhase = ''
          runHook preInstall
          mkdir -p "$out"
          cp -r . "$out/"
          runHook postInstall
        '';
        meta = {
          description = "Pinned stable Chromium Embedded Framework SDK";
          homepage = "https://github.com/chromiumembedded/cef";
          license = lib.licenses.bsd3;
          platforms = [ system ];
        };
      };
      package = pkgs.stdenv.mkDerivation {
        pname = "figma-gtk";
        version = "0.1.0";
        src = lib.cleanSourceWith {
          src = ./.;
          filter = path: type:
            !(builtins.elem (baseNameOf path) [ ".deps" "build" ".git" ".direnv" "result" ]);
        };
        nativeBuildInputs = buildTools ++ [ pkgs.autoPatchelfHook pkgs.wrapGAppsHook4 ];
        buildInputs = uiLibraries ++ chromiumLibraries;
        cmakeFlags = [ "-DCEF_ROOT=${cefSdk}" ];
        doCheck = true;
        checkPhase = ''
          runHook preCheck
          ctest --output-on-failure
          runHook postCheck
        '';
        meta = {
          description = "A native GNOME workspace for Figma";
          license = lib.licenses.gpl3Only;
          mainProgram = "figma-gtk";
          platforms = [ system ];
        };
      };
    in {
      packages.${system} = { default = package; figma-gtk = package; cef-sdk = cefSdk; };
      apps.${system}.default = { type = "app"; program = "${package}/bin/figma-gtk"; meta.description = package.meta.description; };
      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = buildTools ++ [ pkgs.python3 pkgs.xorg-server pkgs.ripgrep ];
        buildInputs = uiLibraries ++ chromiumLibraries;
        LD_LIBRARY_PATH = lib.makeLibraryPath chromiumLibraries;
        CEF_ROOT = cefSdk;
        FIGMA_GTK_XTEST_LIBRARY = "${pkgs.libXtst}/lib/libXtst.so.6";
      };
      checks.${system}.build = package;
      formatter.${system} = pkgs.nixfmt;
    };
}
