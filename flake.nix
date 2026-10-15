{
  description = "A GTK 4 and libadwaita Figma client with Chromium";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux"; # The pinned CEF distribution targets Linux x86_64.
      pkgs = import nixpkgs { inherit system; };
      lib = pkgs.lib;
      package = import ./package.nix { inherit pkgs; };
    in {
      packages.${system} = { default = package; select-for-figma = package; cef-sdk = package.cefSdk; };
      apps.${system}.default = { type = "app"; program = "${package}/bin/select-for-figma"; meta.description = package.meta.description; };
      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config sysprof nodejs python3 xorg-server ripgrep ];
        buildInputs = package.buildInputs;
        LD_LIBRARY_PATH = lib.makeLibraryPath package.cefSdk.buildInputs;
        CEF_ROOT = package.cefSdk;
        FIGMA_GTK_XTEST_LIBRARY = "${pkgs.libXtst}/lib/libXtst.so.6";
      };
      checks.${system}.build = package;
      formatter.${system} = pkgs.nixfmt;
    };
}
