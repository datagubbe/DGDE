{ clang-tools
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
  name = "dgde";
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
