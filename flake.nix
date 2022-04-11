{
  description = "A desktop environment for Datagubbar";

  inputs.nixpkgs.url = "nixpkgs/nixos-21.11";
  outputs = { self, nixpkgs }:
  let
    supportedSystems = [ "x86_64-linux" ];
    forAllSystems = f: nixpkgs.lib.genAttrs supportedSystems (system: f system);
    pkgs = forAllSystems (sys: import nixpkgs {
      system = sys;
    });
    drv = forAllSystems (sys: pkgs."${sys}".callPackage ./dgde.nix { });
  in
    {
      defaultPackage = forAllSystems (sys: drv."${sys}");
    };
}
