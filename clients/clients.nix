{ clang-tools
, meson
, libGL
, ninja
, pkg-config
, stdenv
, wayland
, wayland-protocols
}:

stdenv.mkDerivation {
  name = "dgde-clients";
  src = ./.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    clang-tools
  ];

  buildInputs = [
    wayland
    wayland-protocols
    libGL
  ];
}
