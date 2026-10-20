#pragma once

class QWidget;

// Stops the mouse wheel from silently changing a spin box / combo box the user
// only happened to hover — the classic mis-configuration in a scrollable
// settings page. After install(), a wheel over one of those widgets adjusts its
// value only once the user has explicitly LEFT-CLICKED it; the wheel is handed
// to the parent (i.e. the page scrolls) until then, and again as soon as focus
// leaves. Keyboard focus alone (Tab) does not arm a widget.
//
// Call once per dialog, after its widgets exist: WheelGuard::install(this).
// It covers every QAbstractSpinBox and QComboBox below `root`.
namespace WheelGuard {
void install(QWidget* root);
}
