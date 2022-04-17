{ clang-tools
, lib
, libxkbcommon
, meson
, ninja
, pixman
, pkg-config
, stdenv
, systemdMinimal
, wayland
, wayland-protocols
, wlroots
}:
stdenv.mkDerivation {
  name = "dgde-compositor";
  src = ./.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    clang-tools
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
