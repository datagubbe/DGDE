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
    env = (pkgs.buildFHSUserEnv {
      name = "dgde-env";
      targetPkgs = pkgs: [ compositor clients];
      runScript = "nixGL dgde";
    });
  in
    {
      packages = {
        default = compositor;
        inherit compositor clients;
      };

      apps = {
        default = {
          type = "app";
          program = "${env}/bin/dgde-env";
        };
      };
    });
}
