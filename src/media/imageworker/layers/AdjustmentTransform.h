// SPDX-License-Identifier: MIT
//
// AdjustmentTransform -- per-pixel adjustment payloads for adjustment layers.
//
// Contract (M2-6b):
//
//   A 1D LUT (256x1 CV_8U) can only express a per-channel function:
//   Curves and Levels really are one such function, so they travel as a
//   LUT. HSL / BlackWhite / ChannelMixer are NOT per-channel functions:
//
//     HSL          shifts hue and saturation per colour band, so the
//                  result of a pixel depends on the hue of THAT pixel.
//     BlackWhite   turns every pixel into grey using six colour-weighted
//                  mixers, so the grey value depends on the hue band the
//                  pixel falls into.
//     ChannelMixer is a 3x3 matrix over the R/G/B channels.
//
//   None of them can be squeezed into a 256-entry table. So an
//   adjustment layer carries BOTH:
//
//     Layer::adjustmentLut       the 1D LUT (Curves / Levels / per-channel
//                                Curves), applied with cv::LUT
//     Layer::adjustmentParams    this file's AdjustmentParams, applied
//                                per pixel
//
//   Exactly one of them is active at a time; AdjustmentParams::kind
//   says which. kind == AdjustKind::Lut means "use the LUT", any other
//   kind means "use the per-pixel transform".
//
// Tile safety:
//
//   Every transform here is a pure per-pixel function: the result of a
//   pixel depends on that pixel alone, never on its neighbours. That is
//   exactly the property cv::LUT has, and exactly the property the
//   compositor's tile splitting needs -- running the transform per tile
//   is bit-identical to running it on the whole canvas. (Contrast the
//   SmartFilter chain, whose blur kernels cross tile seams.)
//
// Alpha:
//
//   The transforms touch the COLOUR channels only. Running alpha through
//   a colour transform would scramble the canvas transparency, so the
//   render paths always split BGRA first and re-merge the original alpha
//   afterwards.

#ifndef ADJUSTMENTTRANSFORM_H
#define ADJUSTMENTTRANSFORM_H

#include <opencv2/core.hpp>

#include <QVector>

namespace layers {

// The kind of per-pixel transform carried by an adjustment layer.
enum class AdjustKind {
    Lut,          // 1D LUT (curves, levels) -- the existing path
    Hsl,          // hue / saturation / lightness
    BlackWhite,   // 6 colour-weighted mixers
    ChannelMixer, // 3x3 matrix
};

// The payload of a per-pixel adjustment.
struct AdjustmentParams {
    AdjustKind kind = AdjustKind::Lut;

    // HSL: 8 hue bands (master + 7 colour bands) and 8 saturation bands,
    // plus a global lightness shift. The layout matches
    // AdjustmentPanel::HslParams (hueShifts / satShifts / lightness)
    // so the existing dialogs and panel need no rework.
    //
    // Band order (index 0 is the master band, which applies to every
    // pixel; 1..7 are the colour bands):
    //   0 Master, 1 Reds, 2 Yellows, 3 Greens, 4 Cyans, 5 Blues,
    //   6 Magentas, 7 (reserved)
    QVector<int> hueShifts;   // [-180, 180] degrees
    QVector<int> satShifts;   // [-100, 100] percent
    int         lightness = 0; // [-100, 100] percent

    // B&W: 6 mixers in the order Reds / Yellows / Greens / Cyans /
    // Blues / Magentas, each 0..200 (100 = neutral).
    QVector<double> bwMixers;

    // ChannelMixer: 3x3 matrix, row-major, each 0..200 (200 = 100%).
    //   cmMatrix[0..2] is the RED output row   (m00, m01, m02)
    //   cmMatrix[3..5] is the GREEN output row  (m10, m11, m12)
    //   cmMatrix[6..8] is the BLUE output row   (m20, m21, m22)
    //
    // The rows are the OUTPUT channels (red, green, blue) and the
    // columns are the INPUT channels (red, green, blue), so the red
    // row reads (red from red, red from green, red from blue).
    double cmMatrix[9] = {200, 0, 0, 0, 200, 0, 0, 0, 200};
    bool   cmMonochrome = false;

    // True when the params describe a transform that would change
    // nothing. Used to keep the render paths from doing pointless work
    // and to keep identity params byte-exact.
    bool isIdentity() const;

    // Two payloads are equal when they describe the same transform.
    // The undo stack uses this to tell whether an adjustment actually
    // changed (a slider drag that ends on the old value must not push
    // an undo command).
    bool operator==(const AdjustmentParams& other) const;
    bool operator!=(const AdjustmentParams& other) const
    {
        return !(*this == other);
    }
};

// True when `params` describes no transform at all (identity).
bool adjustmentParamsAreIdentity(const AdjustmentParams& p);

// Apply the transform to a BGR (3-channel) image.
//
//   bgr : CV_8UC3 BGR input
//   p   : the transform payload
//   out : CV_8UC3 BGR result. Left untouched when the params are the
//         identity, so an identity transform is a byte-exact no-op.
//
// Returns false when the params describe no transform (identity) or the
// input is unusable; in that case `out` is not written.
bool applyAdjustmentTransform(const cv::Mat& bgr, const AdjustmentParams& p, cv::Mat& out);

// Apply a set of per-channel LUTs to a BGR (3-channel) image.
//
//   channelLuts : empty  -> `lut` is applied to all three channels
//                 1 entry -> `lut` is applied to all three channels
//                 3 entries-> entry 0 to B, entry 1 to G, entry 2 to R
//                           (the OpenCV BGR channel order)
//
// A LUT that is not a usable 256x1 CV_8U table is ignored for that
// channel (the channel is then left unchanged).
//
// This is the Curves-with-a-channel-selector support: the Curves
// dialog already offers RGB / R / G / B, but a single LUT could not
// express "lift red only".
bool applyChannelLuts(const cv::Mat& bgr,
                      const cv::Mat& lut,
                      const QVector<cv::Mat>& channelLuts,
                      cv::Mat& out);

}   // namespace layers

#endif   // ADJUSTMENTTRANSFORM_H
