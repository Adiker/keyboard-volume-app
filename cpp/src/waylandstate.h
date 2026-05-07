#pragma once

// Set in main() before QApplication is constructed, read-only thereafter.
// true  = native Wayland session + zwlr_layer_shell_v1 confirmed available
//         → OSD uses LayerShellQt for positioning
// false = X11, XWayland fallback, or layer-shell unavailable
//         → OSD uses move() / QWindow::setPosition() as before
extern bool g_nativeWayland;
