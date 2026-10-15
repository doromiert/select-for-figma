{ pkgs }:
let
  lib = pkgs.lib;
  system = "x86_64-linux";
  uiLibraries = with pkgs; [ gtk4 libadwaita json-glib libepoxy fontconfig freetype libsodium libsecret libsoup_3 ];
  chromiumLibraries = with pkgs; [
    stdenv.cc.cc.lib glib nss nspr dbus at-spi2-core systemd
    cups libdrm libxkbcommon libGL libgbm expat alsa-lib pango cairo
    libX11 libXcomposite libXdamage libXext libXfixes libXrandr libxcb
  ];
  buildTools = with pkgs; [ cmake ninja pkg-config sysprof nodejs ];
  cefSdk = pkgs.stdenv.mkDerivation {
    pname = "select-for-figma-cef-sdk";
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
    pname = "select-for-figma";
    version = "1.0.2";
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
    doInstallCheck = true;
    installCheckPhase = ''
      runHook preInstallCheck
      test -x "$out/bin/select-for-figma"
      test -x "$out/lib/select-for-figma/select-for-figma"
      test ! -L "$out/lib/select-for-figma/select-for-figma"
      grep -Fxq 'Exec=select-for-figma %U' "$out/share/applications/io.github.doromiert.SelectForFigma.desktop"
      runHook postInstallCheck
    '';
    passthru = { inherit cefSdk; };
    meta = {
      description = "A native GNOME workspace for Figma";
      license = lib.licenses.gpl3Only;
      mainProgram = "select-for-figma";
      platforms = [ system ];
    };
  };
in package
