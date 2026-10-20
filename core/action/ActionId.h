#pragma once

#include <QHash>

// Stable identifiers for every user-triggerable action. Hotkeys, the tray menu,
// in-overlay/pin keyboard handlers, and the CLI all dispatch through these via
// ActionRegistry — see claude-plan/Implementation-Plan.md ("Action/Command layer").
//
// Not every id has a handler yet; later stages fill them in. The enum is the
// stable contract, not the implementation status.
enum class ActionId {
    None = 0,

    // Global
    Snip,
    SnipCopy,        // snip whose first confirm copies + closes (no annotate phase)
    CustomSnip,      // snip with the area/delay preset from snip/customSnip/*
    SnipActiveWindow,// snip preset to the foreground window's frame
    // (Quick snip is not an ActionId — it's a hold-modifier + drag gesture wired directly in
    //  app/Application via snip/quickSnip/trigger, not the keymap/hotkey system.)
    Paste,
    HideAllPins,
    CloseAllPins,
    DestroyAllPins,
    ToggleClickThrough,
    PickColor,       // freeze the screen and pick a pixel colour with the loupe
    ScanBarcode,     // snip a region, decode a barcode/QR in it, copy + close
    OpenPreferences,
    Quit,

    // Snip context
    Snip_Copy,
    Snip_Save,
    Snip_QuickSave,
    Snip_Pin,
    Snip_Abort,
    Snip_ToggleDetect,
    Snip_Fullscreen,
    Snip_RepeatLast,
    Snip_HistPrev,
    Snip_HistNext,
    Snip_ToggleCursor,
    Snip_Refresh,    // re-grab the frozen desktop (tap = once, hold = continuous)

    // Pin context
    Pin_RotateCW,
    Pin_RotateCCW,
    Pin_FlipH,
    Pin_FlipV,
    Pin_Reset,
    Pin_Thumbnail,
    Pin_Close,
    Pin_Destroy,
    Pin_SelectAll,
    Pin_ReplaceFromClipboard,
    Pin_CopyText,

    // Edit context
    Edit_Undo,
    Edit_Redo,
    Edit_ClearAll,

    // Common
    Color_Copy,
};

// Enable ActionId as a QHash key.
inline size_t qHash(ActionId id, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(id), seed);
}
