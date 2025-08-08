{ pkgs ? import <nixpkgs> {} }:
let
  pulseVisualizer = pkgs.callPackage ./pulse-visualizer.nix {};
in
pkgs.mkShell {
  name = "pulse-visualizer";

  buildInputs = [ pulseVisualizer ];
}
