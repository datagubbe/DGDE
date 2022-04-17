{
  description = "A desktop environment for Datagubbar";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.11";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
  let
    pkgs = nixpkgs.legacyPackages."${system}";
    compositor = pkgs.callPackage ./compositor/compositor.nix { };
    clients = pkgs.callPackage ./clients/clients.nix { };
  in
    {
      defaultPackage = compositor;

      packages = {
        inherit compositor clients;
      };
    });
}
