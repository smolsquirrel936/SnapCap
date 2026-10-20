#pragma once

#include "platform/platform.h"

// Windows autostart / elevation backend (General-tab "Run on system startup" +
// "As administrator"). See platform::IAutostart for the contract.
//
// Two mutually-exclusive backing forms, keyed by the app name "SnapCap":
//   * non-elevated -> HKCU\Software\Microsoft\Windows\CurrentVersion\Run value
//                     (quoted exe path); needs no elevation.
//   * elevated     -> a highest-privilege ("/rl highest") logon Scheduled Task,
//                     managed via schtasks.exe. Creating/deleting it requires the
//                     current process to itself be elevated.
// apply() removes the other form before adding the requested one, so the app is
// never registered to auto-start twice.
class WinAutostart : public platform::IAutostart {
public:
    bool currentProcessElevated() const override;
    bool isEnabled() const override;
    bool isElevated() const override;
    bool apply(bool enabled, bool elevated) override;
    bool relaunchElevated() override;

private:
    // Run-key form (registry).
    bool runKeyPresent() const;
    bool setRunKey(bool present) const;

    // Scheduled-task form (schtasks.exe).
    bool taskPresent() const;
    bool createTask() const;    // needs elevation
    bool deleteTask() const;    // needs elevation
};
