// SPDX-License-Identifier: MIT
//
// AbrParser - P1.1 (2026-09-15) Brush full implementation
//   Adobe Brush (.abr) format reader. Supports ABRv6 computed brushes ('m' type).
//   ABRv9 / sampled brushes ('t' type) deferred to P1.1 follow-up.
//
//   Format reference (ABRv6, all fields big-endian):
//     u16  magic       = 0x3842  ('8B')
//     u16  version     = 6
//     u32  unknown     = 0x00000100
//     u32  count       brush count
//   For each brush:
//     u32  tag         'm' (0x6D) or 't' (0x74)
//     u32  length      bytes following
//   For 'm' computed brush (v6):
//     u32  unknown1
//     u32  nameLen     (includes 2-byte len prefix)
//     utf16be name (nameLen - 2 bytes)
//     u32  shortForm   (1 = short)
//     u32  spacing
//     u32  diameter
//     u32  hardness
//     if !shortForm: u32 angle, u32 roundness, u32 hardness2, ...
//   ABRv9 adds descriptor-based key/value, deferred.

#pragma once

#include <QList>
#include <QString>

#include "BrushPreset.h"

namespace brushes {

class AbrParser {
public:
    // Result of parsing one .abr file.
    struct Result {
        bool        ok = false;
        QString     errorMessage;
        int         version = 0;
        QList<BrushPreset> presets;
    };

    // Parse file. Returns Result with ok=true + presets list on success.
    static Result parseFile(const QString &filePath);

    // Parse from in-memory buffer (for testing).
    static Result parseBuffer(const char *data, int size, const QString &sourceName = QString());
};

} // namespace brushes