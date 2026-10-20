#pragma once

#include <QByteArray>
#include <QString>

// Parse an OpenType/TrueType 'name' table and return the font's *native* family
// name in its own script (e.g. "微软雅黑" for Microsoft YaHei, "游ゴシック" for Yu
// Gothic), or an empty string for fonts with no CJK name (Latin fonts).
//
// Why: on an English Windows the OS/DirectWrite reports every family under its
// English name, so the font picker would list "Microsoft YaHei" instead of the
// name the user recognises. The native name lives in the font's own 'name' table
// (Windows platform records, Chinese/Japanese/Korean language IDs); this pulls it
// out. Kept pure (bytes -> QString) so it is headless-unit-testable; the caller
// (ui/FontPickerDialog) obtains the raw table via QRawFont::fontTable("name").
namespace core {
QString parseNativeFamilyName(const QByteArray& nameTable);
}
