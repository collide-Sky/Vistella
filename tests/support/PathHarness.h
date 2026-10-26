// =============================================================================
//  tests/support/PathHarness.h
//
//  Shared facilities for capability-path tests (tst_Path_*).
//
//  Why it exists:
//    Every existing test deliberately avoids constructing ImageWindow
//    (see tst_LayerPanelIntegration.cpp:17 - "single tests must not
//    new ImageWindow, it needs QApplication + lots of UI dependencies").
//    Consequence: nothing in the suite can ever verify a *user* path
//    (open -> edit -> undo -> redo -> save -> reopen). Only class-level
//    behaviour is covered, which is exactly how 81 green tests coexisted
//    with 34 unaudited P0 defects.
//
//  What it provides:
//    - deterministic synthetic images (no external asset dependency)
//    - QImage <-> cv::Mat conversion matching the project's BGR convention
//    - real keyboard event delivery for undo / redo
//    - a temp-dir scoped file for save / reopen round-trips
// =============================================================================

#pragma once

#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>

namespace vistella_test {

// ---------------------------------------------------------------------------
//  Deterministic synthetic image.
//
//  Four quadrants in distinct colours plus a 2px border, so a test can
//  assert "this pixel changed" / "this pixel did not change" without
//  needing a photo from the repository.
// ---------------------------------------------------------------------------
inline QImage makeTestImage(int size = 256)
{
    QImage img(size, size, QImage::Format_RGB888);
    const int half = size / 2;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            QRgb c;
            if (x < half && y < half) {
                c = qRgb(220, 40, 40);      // top-left    : red
            } else if (x >= half && y < half) {
                c = qRgb(40, 200, 60);      // top-right   : green
            } else if (x < half) {
                c = qRgb(50, 60, 210);      // bottom-left : blue
            } else {
                c = qRgb(235, 235, 235);   // bottom-right: white
            }
            img.setPixel(x, y, c);
        }
    }

    // 1px black border -> lets a test detect an off-by-one crop/shift.
    for (int i = 0; i < size; ++i) {
        img.setPixel(i, 0, qRgb(0, 0, 0));
        img.setPixel(i, size - 1, qRgb(0, 0, 0));
        img.setPixel(0, i, qRgb(0, 0, 0));
        img.setPixel(size - 1, i, qRgb(0, 0, 0));
    }
    return img;
}

// Write the synthetic image to disk as PNG.
inline bool writeTestImage(const QString& path, int size = 256)
{
    return makeTestImage(size).save(path, "PNG");
}

// ---------------------------------------------------------------------------
//  QImage <-> cv::Mat. The project stores images as BGR (OpenCV order).
// ---------------------------------------------------------------------------
inline cv::Mat qImageToBgr(const QImage& img)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat bgr(rgb.height(), rgb.width(), CV_8UC3);
    for (int y = 0; y < rgb.height(); ++y) {
        const uchar* line = rgb.constScanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            bgr.at<cv::Vec3b>(y, x) = cv::Vec3b(line[x * 3 + 2],   // R -> B
                                                  line[x * 3 + 1],   // G -> G
                                                  line[x * 3 + 0]);  // B -> R
        }
    }
    return bgr;
}

// Read a PNG from disk into a BGR cv::Mat.
inline cv::Mat readBgr(const QString& path)
{
    return cv::imread(path.toStdString(), cv::IMREAD_COLOR);
}

// ---------------------------------------------------------------------------
//  Real keyboard delivery.
//
//  ImageWindow::keyPressEvent handles Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, so the
//  path tests exercise the actual user gesture instead of calling undoPublic()
//  directly. Sending the event to the window keeps it working even when the
//  window was never shown.
// ---------------------------------------------------------------------------
inline void sendUndo(QWidget* target)
{
    QTest::keyClick(target, Qt::Key_Z, Qt::ControlModifier);
}

inline void sendRedo(QWidget* target)
{
    QTest::keyClick(target, Qt::Key_Y, Qt::ControlModifier);
}

// Send a plain key (no modifier).
inline void sendKey(QWidget* target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QTest::keyClick(target, key, mods);
}

// ---------------------------------------------------------------------------
//  Let the event loop drain so async render / signal work settles before
//  an assertion reads the result.
// ---------------------------------------------------------------------------
inline void settle(int ms = 60)
{
    QTest::qWait(ms);
}

// ---------------------------------------------------------------------------
//  Pixel probe helpers.
// ---------------------------------------------------------------------------
inline QColor pixelAt(const QImage& img, int x, int y)
{
    if (img.isNull() || x < 0 || y < 0 || x >= img.width() || y >= img.height()) {
        return QColor();
    }
    return QColor::fromRgb(img.pixel(x, y));
}

// True when the two images differ at the given pixel (with a tolerance).
inline bool pixelDiffers(const QImage& a, const QImage& b, int x, int y, int tolerance = 12)
{
    const QColor ca = pixelAt(a, x, y);
    const QColor cb = pixelAt(b, x, y);
    if (!ca.isValid() || !cb.isValid()) {
        return true;
    }
    return qAbs(ca.red() - cb.red()) > tolerance
        || qAbs(ca.green() - cb.green()) > tolerance
        || qAbs(ca.blue() - cb.blue()) > tolerance;
}

// Largest per-channel difference between two images.
//
// A single pixel probe is unreliable: a blur does not change a pixel deep
// inside a flat region, so "the pixel is unchanged" would wrongly look like
// "the edit did not happen". Comparing whole images catches the edit
// wherever it actually landed (e.g. a gradient at a region border).
inline int maxPixelDelta(const QImage& a, const QImage& b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return 255;
    }
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            const int d = qMax(qAbs(qRed(pa) - qRed(pb)),
                          qMax(qAbs(qGreen(pa) - qGreen(pb)),
                               qAbs(qBlue(pa) - qBlue(pb))));
            if (d > worst) {
                worst = d;
            }
        }
    }
    return worst;
}

// Number of pixels that differ by more than the tolerance.
inline int changedPixelCount(const QImage& a, const QImage& b, int tolerance = 12)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return -1;
    }
    int n = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            const int d = qMax(qAbs(qRed(pa) - qRed(pb)),
                          qMax(qAbs(qGreen(pa) - qGreen(pb)),
                               qAbs(qBlue(pa) - qBlue(pb))));
            if (d > tolerance) {
                ++n;
            }
        }
    }
    return n;
}

}  // namespace vistella_test

// =============================================================================
//  Qt message recorder.
//
//  A capability path must not produce Qt warnings. "QStackedWidget:
//  setCurrentWidget: widget not contained in stack" on window construction
//  is exactly the kind of defect that a green class-level test suite never
//  surfaces, so the path tests record and assert on it.
//
//  Usage:
//      QtMessageRecorder rec;
//      ... run the path ...
//      QCOMPARE(rec.warningCount(), 0);
// =============================================================================

class QtMessageRecorder
{
public:
    QtMessageRecorder()
    {
        m_prev = s_active;
        s_active = this;
        m_handler = qInstallMessageHandler(&QtMessageRecorder::handler);
    }

    ~QtMessageRecorder()
    {
        qInstallMessageHandler(m_handler);
        s_active = m_prev;
    }

    int warningCount() const { return m_warningCount; }
    int criticalCount() const { return m_criticalCount; }
    const QStringList& messages() const { return m_messages; }

    bool contains(const QString& fragment) const
    {
        for (const QString& m : m_messages) {
            if (m.contains(fragment, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

private:
    static void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
    {
        // Keep the messages visible: the recorder must never silence the log.
        if (!s_active) {
            return;
        }
        if (type == QtWarningMsg) {
            ++s_active->m_warningCount;
        } else if (type == QtCriticalMsg || type == QtFatalMsg) {
            ++s_active->m_criticalCount;
        }
        s_active->m_messages.append(msg);

        const char* lvl = "INFO";
        if (type == QtWarningMsg) {
            lvl = "WARN";
        } else if (type == QtCriticalMsg || type == QtFatalMsg) {
            lvl = "CRIT";
        } else if (type == QtDebugMsg) {
            lvl = "DEBUG";
        }
        fprintf(stderr, "[%s] %s (%s:%d)\n", lvl, qPrintable(msg),
                ctx.file ? ctx.file : "?", ctx.line);
    }

    static inline QtMessageRecorder* s_active = nullptr;

    QtMessageRecorder* m_prev = nullptr;
    QtMessageHandler m_handler = nullptr;
    int m_warningCount = 0;
    int m_criticalCount = 0;
    QStringList m_messages;
};
