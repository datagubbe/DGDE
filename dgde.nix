{ stdenv, meson, ninja, wlroots, wayland, wayland-protocols, pkg-config, systemdMinimal, pixman, libxkbcommon }:
stdenv.mkDerivation {
  name = "dgde";
  src = ./.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    pixman
    systemdMinimal
    wlroots
    wayland
    wayland-protocols
    libxkbcommon
  ];
}
