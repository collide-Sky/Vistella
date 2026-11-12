// SPDX-License-Identifier: MIT
//
// VstProject
//
// The Vistella project format (.vst) -- M2-1.
//
// THE PROBLEM THIS SOLVES
//
//   Before this, "save" wrote a FLAT PICTURE: the composited pixels of
//   the whole document went to a PNG/JPEG and everything else was
//   thrown away -- the layers, the masks, the vector paths, the text,
//   the adjustment parameters, the smart objects. Reopening the file
//   gave a single bitmap. The audit (docs/CODE_AUDIT_2026-10-26.md)
//   listed this as A-PROJECTFORMAT.
//
// THE FORMAT
//
//   A .vst file is a small container:
//
//     +------------------------------------------------------+
//     | "VSTPROJ\0\0"  magic (8 bytes)                      |
//     | uint32 manifestOffset  (where the manifest starts)   |
//     | uint32 manifestLength  (the manifest's byte count)  |
//     | uint32 payloadOffset   (where the payload starts)    |
//     | uint32 payloadLength   (the payload's byte count)    |
//     | uint32 magic2          (a second copy of the magic) |
//     +------------------------------------------------------+
//     | manifest: UTF-8 JSON                                  |
//     |   - the canvas size                                    |
//     |   - one entry per layer (kind, name, opacity, ...)     |
//     |   - per-layer references into the payload               |
//     +------------------------------------------------------+
//     | payload: the binary blobs                              |
//     |   - layer pixels (raw BGRA)                            |
//     |   - pixel masks (raw 8-bit grey)                       |
//     |   - LUTs (raw 256 bytes)                               |
//     +------------------------------------------------------+
//
//   Why JSON for the manifest: the layer list is small, it changes
//   shape as the format grows, and a human can read it when a file
//   goes wrong. The pixels are the big part and they stay raw.
//
//   The manifest holds the STRUCTURE and the payload holds the BULKS.
//   That split is what lets the format grow: a new per-layer field is
//   a JSON key, and a new bulk is a new payload entry.
//
// WHY NOT A ZIP
//
//   QZipReader / QZipWriter are PRIVATE in Qt 6.11
//   (QtCore/6.11.1/QtCore/private/qzipreader_p.h), so they cannot be
//   linked from outside. Rather than take a dependency on a zip
//   library for one container, the format is self-contained.
//
// THE TWO PASSES
//
//   save() writes the payload first (it does not know the sizes
//   until the blobs exist) and the manifest second, then patches the
//   header. load() reads the header, seeks to the manifest, and
//   resolves the payload references.
//
// CONTRACT
//
//   A .vst file must round-trip the WHOLE layer stack: every layer,
//   its pixels, its mask, its text, its vector paths, its adjustment
//   parameters and its blend / opacity / visibility / lock / link
//   state. A save that keeps only the pixels is a flat picture with
//   a different extension, and the test asserts against exactly that.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

namespace layers { class Layer; class LayerStack; }

namespace project {

// The file magic. Two copies: at the start and after the header, so a
// truncated file can be detected without parsing the header.
extern const char* const kVstMagic;      // "VSTPROJ\0" (8 bytes)
constexpr quint32 kVstFormatVersion = 1;

// One blob inside the payload.
struct VstBlob {
    QString name;      // the key used in the manifest
    QByteArray bytes;  // the raw content
};

// The result of a save / load.
struct VstResult {
    bool ok = false;
    QString error;     // empty when ok
};

// Save a layer stack to a .vst file.
//
//   Returns false (and fills `error`) when the file cannot be written.
VstResult saveVst(const QString& path, const layers::LayerStack* stack);

// Load a layer stack from a .vst file.
//
//   On success the stack is REPLACED by the file's content: the old
//   layers are cleared first, so a load never leaves a mix of old and
//   new layers.
VstResult loadVst(const QString& path, layers::LayerStack* stack);

// The same, but on a byte buffer instead of a file.
//
//   The buffer form is what the auto-save and the crash-recovery use:
//   they keep the bytes in memory (or in a scratch file) and do not
//   want a temp path just to test the round trip.
VstResult saveVstToBytes(QByteArray& out, const layers::LayerStack* stack);
VstResult loadVstFromBytes(const QByteArray& bytes, layers::LayerStack* stack);

// ===== the pieces, exposed for testing =====

// Serialise one layer into its manifest JSON object plus its blobs.
//
//   `blobs` receives the binary parts; the returned object references
//   them by name.
QJsonObject layerToJson(const layers::Layer& layer, QList<VstBlob>& blobs);

// Rebuild one layer from its manifest object plus the blob table.
layers::Layer layerFromJson(const QJsonObject& obj,
                            const QHash<QString, QByteArray>& blobs);

} // namespace project
