// SPDX-License-Identifier: MIT
//
// AdjustmentTransform -- per-pixel adjustment payloads for adjustment layers.
// See AdjustmentTransform.h for the contract.

#include "AdjustmentTransform.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace layers {

namespace {

// ---- shared constants -----------------------------------------------------

// The number of bands in the HSL layout: master + 7 colour bands.
//   0 Master, 1 Reds, 2 Yellows, 3 Greens, 4 Cyans, 5 Blues,
//   6 Magentas, 7 (reserved)
constexpr int kHslBandCount = 8;

// The number of B&W mixers: Reds / Yellows / Greens / Cyans / Blues /
// Magentas.
constexpr int kBwMixerCount = 6;

// The ChannelMixer scale: 200 == 100%.
constexpr double kCmScale = 200.0;

// A 256x1 CV_8U table is what cv::LUT accepts.
bool isUsableLut(const cv::Mat& lut)
{
    return !lut.empty()
        && lut.rows == 256
        && lut.cols == 1
        && lut.type() == CV_8U;
}

// The colour band of a hue, in the 0..179 OpenCV hue scale.
//
//   Reds      0   (0 deg)
//   Yellows  30  (60 deg)
//   Greens    60  (120 deg)
//   Cyans     90  (180 deg)
//   Blues    120  (240 deg)
//   Magentas 150  (300 deg)
//
// Each band spans 30 hue units (60 degrees) centred on its name, so
// the boundary between two bands is at the midpoint between their
// centres. That gives, for the band index b (0-based over the 6
// colours): the band covers hue values in
//   [b*30 - 15, b*30 + 15)  (mod 180)
// with the first band (Reds) wrapping around zero.
int colourBandOfHue(int hue)
{
    // hue is in 0..179; normalise defensively.
    hue = ((hue % 180) + 180) % 180;
    // Shift by 15 so the band centres land on multiples of 30.
    const int shifted = (hue + 15) % 180;
    int band = shifted / 30;      // 0..5
    if (band > 5) band = 5;
    return band;
}

// The hue shifts / saturation shifts of the band a pixel belongs to.
//
// The master band (index 0) applies to EVERY pixel; the colour band
// (index 1..6) applies on top of the master. A pixel therefore gets
// masterShift + colourShift.
void hslShiftsForHue(const QVector<int>& hueShifts,
                     const QVector<int>& satShifts,
                     int hue,
                     int& hueShiftOut,
                     int& satShiftOut)
{
    hueShiftOut = 0;
    satShiftOut = 0;
    if (!hueShifts.isEmpty()) hueShiftOut += hueShifts.at(0);
    if (!satShifts.isEmpty())  satShiftOut  += satShifts.at(0);

    // Colour band index in the HSL layout is band + 1 (0 is master).
    const int bandIndex = colourBandOfHue(hue) + 1;
    if (hueShifts.size() > bandIndex) hueShiftOut += hueShifts.at(bandIndex);
    if (satShifts.size()  > bandIndex) satShiftOut  += satShifts.at(bandIndex);
}

// ---- HSL ------------------------------------------------------------------

// Build a 256-entry table that maps a hue (0..179) to the SHIFTED hue.
//
// The band shift is looked up from the hueShifts vector: the master
// band (index 0) plus the colour band (index band+1).
//
// Units: hueShifts holds DEGREES, but OpenCV's 8-bit hue plane runs
// 0..179 for 0..358 degrees, so one degree is HALF a hue unit. A +60
// degree shift is therefore +30 hue units.
//
// The table holds the FINAL hue (already wrapped into 0..179), so the
// whole per-band hue shift is a single cv::LUT and no modular add is
// needed at run time.
cv::Mat hueShiftByHueTable(const QVector<int>& hueShifts)
{
    cv::Mat table(256, 1, CV_8U);
    for (int h = 0; h < 256; ++h) {
        const int hue = ((h % 180) + 180) % 180;
        const int bandIndex = colourBandOfHue(hue) + 1;
        int degrees = hueShifts.isEmpty() ? 0 : hueShifts.at(0);
        if (hueShifts.size() > bandIndex) degrees += hueShifts.at(bandIndex);
        // Degrees -> OpenCV hue units.
        int units = static_cast<int>(std::lround(degrees / 2.0));
        // A full turn is a no-op, so the shift is clamped to +-179 units.
        units = std::max(-179, std::min(179, units));
        const int shifted = ((hue + units) % 180 + 180) % 180;
        table.at<uchar>(h) = static_cast<uchar>(shifted);
    }
    return table;
}

// Build a 256-entry table that maps a hue (0..179) to the saturation
// shift (percent) of the band that hue belongs to.
cv::Mat satShiftByHueTable(const QVector<int>& satShifts)
{
    cv::Mat table(256, 1, CV_8U);
    for (int h = 0; h < 256; ++h) {
        const int hue = ((h % 180) + 180) % 180;
        const int bandIndex = colourBandOfHue(hue) + 1;
        int shift = satShifts.isEmpty() ? 0 : satShifts.at(0);
        if (satShifts.size() > bandIndex) shift += satShifts.at(bandIndex);
        shift = std::max(-100, std::min(100, shift));
        // Encode as shift + 100 so the unsigned byte can carry it.
        table.at<uchar>(h) = static_cast<uchar>(shift + 100);
    }
    return table;
}

// Apply the HSL transform to a BGR image.
//
//   1. Convert to HSV. OpenCV's 8-bit HSV has H in 0..179 (i.e.
//      0..358 degrees), S and V in 0..255.
//   2. Shift the hue per band and the saturation per band. Both shifts
//      depend on the hue of the pixel, so they are computed as
//      per-hue lookup tables and applied with cv::LUT on the H and S
//      planes.
//   3. Apply the global lightness shift to the value (V) plane.
//   4. Convert back to BGR.
//
// The hue shift is applied with a modular add of the hue-unit shift,
// wrapped into 0..179.
cv::Mat applyHsl(const cv::Mat& bgr, const AdjustmentParams& p)
{
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    std::vector<cv::Mat> planes;
    cv::split(hsv, planes);
    cv::Mat& hPlane = planes[0];
    cv::Mat& sPlane = planes[1];
    cv::Mat& vPlane = planes[2];

    // ---- per-band hue shift ----
    //
    // cv::LUT cannot do a modular add, so the shift is folded into a
    // per-hue table: for every hue value h, the table holds the SHIFT
    // to add (encoded as shift + 180). The add itself is a modular
    // add, done with cv::add (saturating) plus a wrap fix-up.
    //
    // The master band plus the colour band both contribute, so the
    // table is built from both.
    {
        // The table already contains the wrapped result, so the whole
        // per-band hue shift is one LUT.
        cv::Mat shiftedHueTable = hueShiftByHueTable(p.hueShifts);
        cv::LUT(hPlane, shiftedHueTable, hPlane);
    }

    // ---- per-band saturation shift ----
    //
    // The saturation shift is a percentage of the current saturation.
    // It depends on the hue, so the percentage is looked up per hue
    // and the per-pixel multiply is done with cv::LUT on a 256-entry
    // table built from the ORIGINAL hue (the hue shift above does not
    // change which band a pixel belongs to in any meaningful way for
    // the saturation, and using the original hue keeps the two
    // lookups independent).
    {
        cv::Mat originalHue;
        cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
        std::vector<cv::Mat> origPlanes;
        cv::split(hsv, origPlanes);
        originalHue = origPlanes[0];

        cv::Mat pctTable = satShiftByHueTable(p.satShifts);
        cv::Mat pctByHue;
        cv::LUT(originalHue, pctTable, pctByHue);
        cv::Mat pct;
        pctByHue.convertTo(pct, CV_16S, 1.0, -100.0);

        cv::Mat s16;
        sPlane.convertTo(s16, CV_16S);
        // newS = s + s * pct / 100
        //
        // The multiply is POINTWISE, not a matrix product: the Mat * Mat
        // operator is cv::gemm, which only accepts CV_32F / CV_64F and
        // asserts on CV_16S. cv::multiply is the pointwise form.
        //
        // The arithmetic is done in CV_32S: s is at most 255 and pct at
        // most 100 in absolute value, so the product is at most 25500,
        // which fits comfortably in 32 bits.
        cv::Mat s32, pct32;
        s16.convertTo(s32, CV_32S);
        pct.convertTo(pct32, CV_32S);
        cv::Mat delta;
        cv::multiply(s32, pct32, delta);
        delta /= 100;
        cv::Mat newS = s32 + delta;
        newS.convertTo(sPlane, CV_8U);
    }

    // ---- global lightness shift ----
    //
    // Applied to the value (V) plane: V' = V + lightness * 255 / 200.
    // The factor 255/200 maps a +/-100 percent slider onto roughly
    // +/-127 levels, which is the same scale the legacy 1D lightness
    // LUT used (255/400 of the slider range -> +/-64; here the
    // lightness shift is a full-strength edit, so it gets the wider
    // range).
    if (p.lightness != 0) {
        const double shift = static_cast<double>(p.lightness) * 255.0 / 200.0;
        cv::Mat v16;
        vPlane.convertTo(v16, CV_16S);
        cv::Mat newV = v16 + cv::Scalar(shift);
        newV.convertTo(vPlane, CV_8U);
    }

    cv::Mat merged;
    cv::merge(planes, merged);
    cv::Mat out;
    cv::cvtColor(merged, out, cv::COLOR_HSV2BGR);
    return out;
}

// ---- Black & White --------------------------------------------------------

// Compute the grey value of a BGR pixel under the 6 mixers.
//
// Photoshop's Black & White works like this: the pixel's hue decides
// WHICH set of three weights applies, and the pixel's R/G/B values are
// then weighted by those three weights to produce the grey.
//
// The weight of a colour channel is taken from the mixer of the band
// that "owns" that channel for the pixel's hue:
//
//   a red pixel   -> R weighted by Reds,      G by Yellows, B by Magentas
//   a yellow one  -> R by Reds,               G by Greens,  B by Blues
//   ... etc.
//
// Concretely, for the hue band b (0..5 = Reds..Magentas), the three
// weights are the mixers of band b (own colour) and of the two
// adjacent bands, so the weighting blends smoothly across band
// boundaries instead of jumping.
//
//   band b: weight for the channel the band owns  = mixer[b]
//           weight for the neighbouring channels   = the neighbouring
//                                                   mixers
//
// The six mixers map onto the three channels as:
//   Reds    -> R, Yellows -> G, Greens -> G, Cyans -> B, Blues -> B,
//   Magentas-> R
// so each mixer contributes to the channel it is "closest" to.
cv::Vec3b blackWhitePixel(const cv::Vec3b& bgr,
                          const double mixers[kBwMixerCount],
                          int hue)
{
    const int band = colourBandOfHue(hue);

    // The three weights for the R, G and B channels, in that order.
    //
    // For each colour band, the mixer of the band itself weights the
    // channel that band is "made of", and the two neighbours weight
    // their own channels:
    //
    //   Reds    (0): R <- m0 (Reds),    G <- m1 (Yellows), B <- m5 (Magentas)
    //   Yellows (1): R <- m0 (Reds),    G <- m2 (Greens),  B <- m5 (Magentas)
    //   Greens  (2): R <- m5 (Magentas),G <- m2 (Greens),  B <- m3 (Cyans)
    //   Cyans   (3): R <- m5 (Magentas),G <- m4 (Blues),   B <- m3 (Cyans)
    //   Blues   (4): R <- m0 (Reds),    G <- m4 (Blues),   B <- m4 (Blues)
    //   Magentas(5): R <- m5 (Magentas),G <- m1 (Yellows), B <- m4 (Blues)
    //
    // This is the Photoshop weighting: a red pixel is R * Reds +
    // G * Yellows + B * Magentas.
    static const int kWeights[kBwMixerCount][3] = {
        /* Reds     */ { 0, 1, 5 },
        /* Yellows  */ { 0, 2, 5 },
        /* Greens   */ { 5, 2, 3 },
        /* Cyans    */ { 5, 4, 3 },
        /* Blues    */ { 0, 4, 4 },
        /* Magentas */ { 5, 1, 4 },
    };

    const int* w = kWeights[band];
    const double wR = mixers[w[0]];
    const double wG = mixers[w[1]];
    const double wB = mixers[w[2]];

    const double b = bgr[0];
    const double g = bgr[1];
    const double r = bgr[2];

    // The mixers are weights in PERCENT of the neutral 100.
    //
    // The grey value divides by the NEUTRAL total (3 channels x 100),
    // NOT by the sum of the actual weights: dividing by the actual sum
    // would make a uniform change of all six mixers a no-op, and the
    // mixers would lose the ability to change the brightness at all.
    // With the neutral mixers the result is the plain average of the
    // three channels.
    const double grey = (r * wR + g * wG + b * wB) / 300.0;
    const uchar v = cv::saturate_cast<uchar>(grey);
    return cv::Vec3b(v, v, v);
}

cv::Mat applyBlackWhite(const cv::Mat& bgr, const AdjustmentParams& p)
{
    double mixers[kBwMixerCount];
    for (int i = 0; i < kBwMixerCount; ++i) {
        mixers[i] = (i < p.bwMixers.size())
                        ? std::max(0.0, std::min(200.0, p.bwMixers.at(i)))
                        : 100.0;
    }
    // A weight of zero for all three channels would divide by zero.
    // Fall back to the neutral weight in that case.
    if (mixers[0] + mixers[1] + mixers[2] + mixers[3] + mixers[4] + mixers[5]
        <= 0.0) {
        for (int i = 0; i < kBwMixerCount; ++i) mixers[i] = 100.0;
    }

    // The grey value depends on the pixel's hue, so we need the hue
    // per pixel. Convert to HSV once and read the H plane.
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> planes;
    cv::split(hsv, planes);
    const cv::Mat& hPlane = planes[0];

    cv::Mat out = cv::Mat::zeros(bgr.size(), CV_8UC3);
    for (int y = 0; y < bgr.rows; ++y) {
        const uchar* srcRow = bgr.ptr<uchar>(y);
        const uchar* hRow = hPlane.ptr<uchar>(y);
        uchar* dstRow = out.ptr<uchar>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            const cv::Vec3b px(srcRow[x * 3 + 0], srcRow[x * 3 + 1],
                               srcRow[x * 3 + 2]);
            const cv::Vec3b grey = blackWhitePixel(px, mixers, hRow[x]);
            dstRow[x * 3 + 0] = grey[0];
            dstRow[x * 3 + 1] = grey[1];
            dstRow[x * 3 + 2] = grey[2];
        }
    }
    return out;
}

// ---- Channel Mixer --------------------------------------------------------

cv::Mat applyChannelMixer(const cv::Mat& bgr, const AdjustmentParams& p)
{
    // The matrix is stored in Photoshop order: the ROWS are the output
    // channels (red, green, blue) and the COLUMNS are the input
    // channels (red, green, blue):
    //
    //   cmMatrix[0..2]  red output row   (from R, G, B)
    //   cmMatrix[3..5]  green output row (from R, G, B)
    //   cmMatrix[6..8]  blue output row  (from R, G, B)
    //
    // The pixels arrive in OpenCV's BGR order, so the R/G/B values are
    // unpacked in that order before the matrix is applied.
    double m[9];
    for (int i = 0; i < 9; ++i) {
        m[i] = std::max(-200.0, std::min(200.0, p.cmMatrix[i])) / kCmScale;
    }

    if (p.cmMonochrome) {
        // All three output rows become the average of the three rows,
        // so R == G == B everywhere.
        double avg[9];
        for (int col = 0; col < 3; ++col) {
            const double v = (m[col] + m[3 + col] + m[6 + col]) / 3.0;
            avg[col] = v;
        }
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                m[row * 3 + col] = avg[col];
            }
        }
    }

    cv::Mat out = cv::Mat::zeros(bgr.size(), CV_8UC3);
    for (int y = 0; y < bgr.rows; ++y) {
        const uchar* srcRow = bgr.ptr<uchar>(y);
        uchar* dstRow = out.ptr<uchar>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            const double r = srcRow[x * 3 + 2];
            const double g = srcRow[x * 3 + 1];
            const double b = srcRow[x * 3 + 0];
            const double or_ = m[0] * r + m[1] * g + m[2] * b;
            const double og  = m[3] * r + m[4] * g + m[5] * b;
            const double ob  = m[6] * r + m[7] * g + m[8] * b;
            dstRow[x * 3 + 0] = cv::saturate_cast<uchar>(ob);
            dstRow[x * 3 + 1] = cv::saturate_cast<uchar>(og);
            dstRow[x * 3 + 2] = cv::saturate_cast<uchar>(or_);
        }
    }
    return out;
}

}   // namespace

// ---- public API -----------------------------------------------------------

bool AdjustmentParams::isIdentity() const
{
    switch (kind) {
    case AdjustKind::Lut:
        return true;

    case AdjustKind::Hsl: {
        if (lightness != 0) return false;
        for (int v : hueShifts) if (v != 0) return false;
        for (int v : satShifts) if (v != 0) return false;
        return true;
    }

    case AdjustKind::BlackWhite: {
        // An EMPTY mixer set means no Black & White adjustment was
        // configured at all: the transform changes nothing.
        //
        // A CONFIGURED mixer set is never the identity, not even when
        // every mixer sits at the neutral 100: the whole point of a
        // Black & White adjustment layer is to turn the image grey, so
        // neutral mixers still change every pixel.
        return bwMixers.isEmpty();
    }

    case AdjustKind::ChannelMixer: {
        if (cmMonochrome) return false;
        static const double kNeutral[9] = {200, 0, 0, 0, 200, 0, 0, 0, 200};
        for (int i = 0; i < 9; ++i) {
            if (std::abs(cmMatrix[i] - kNeutral[i]) > 1e-9) return false;
        }
        return true;
    }
    }
    return true;
}

bool adjustmentParamsAreIdentity(const AdjustmentParams& p)
{
    return p.isIdentity();
}

bool AdjustmentParams::operator==(const AdjustmentParams& other) const
{
    if (kind != other.kind) return false;
    if (hueShifts != other.hueShifts) return false;
    if (satShifts != other.satShifts) return false;
    if (lightness != other.lightness) return false;
    if (bwMixers != other.bwMixers) return false;
    if (cmMonochrome != other.cmMonochrome) return false;
    for (int i = 0; i < 9; ++i) {
        if (std::abs(cmMatrix[i] - other.cmMatrix[i]) > 1e-9) return false;
    }
    return true;
}

bool applyAdjustmentTransform(const cv::Mat& bgr, const AdjustmentParams& p, cv::Mat& out)
{
    if (bgr.empty() || bgr.type() != CV_8UC3) return false;
    if (p.kind == AdjustKind::Lut) return false;
    if (p.isIdentity()) return false;

    cv::Mat result;
    switch (p.kind) {
    case AdjustKind::Hsl:
        result = applyHsl(bgr, p);
        break;
    case AdjustKind::BlackWhite:
        result = applyBlackWhite(bgr, p);
        break;
    case AdjustKind::ChannelMixer:
        result = applyChannelMixer(bgr, p);
        break;
    case AdjustKind::Lut:
        return false;
    }

    if (result.empty()) return false;
    out = result;
    return true;
}

bool applyChannelLuts(const cv::Mat& bgr,
                      const cv::Mat& lut,
                      const QVector<cv::Mat>& channelLuts,
                      cv::Mat& out)
{
    if (bgr.empty() || bgr.type() != CV_8UC3) return false;

    // Decide which LUT applies to which channel.
    //   channelLuts is empty or has 1 entry -> the single `lut`
    //   channelLuts has 3 entries             -> per-channel
    cv::Mat lutB, lutG, lutR;
    bool any = false;

    if (channelLuts.size() >= 3) {
        // OpenCV BGR order: index 0 = blue, 1 = green, 2 = red.
        lutB = channelLuts.at(0);
        lutG = channelLuts.at(1);
        lutR = channelLuts.at(2);
        any = isUsableLut(lutB) || isUsableLut(lutG) || isUsableLut(lutR);
    } else if (isUsableLut(lut)) {
        lutB = lutG = lutR = lut;
        any = true;
    } else if (channelLuts.size() == 1 && isUsableLut(channelLuts.at(0))) {
        lutB = lutG = lutR = channelLuts.at(0);
        any = true;
    }

    if (!any) return false;

    std::vector<cv::Mat> planes;
    cv::split(bgr, planes);
    if (planes.size() != 3) return false;

    for (int ch = 0; ch < 3; ++ch) {
        const cv::Mat& channelLut = (ch == 0) ? lutB : (ch == 1) ? lutG : lutR;
        if (!isUsableLut(channelLut)) continue;
        cv::LUT(planes[ch], channelLut, planes[ch]);
    }

    cv::Mat merged;
    cv::merge(planes, merged);
    out = merged;
    return true;
}

}   // namespace layers
