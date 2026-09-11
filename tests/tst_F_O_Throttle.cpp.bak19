// SPDX-License-Identifier: MIT
//
// tst_F_O_Throttle - F-O (2026-09-10)
//
// Generic trailing-edge debouncer (core::Throttle, 200ms window) + its
// integration into PropertiesDock.setImageInfo.
//
// Coverage:
//   test 1: Throttle default interval is 200ms; setInterval() updates both
//           the getter and the underlying timer.
//   test 2: 3 rapid trigger() calls within 200ms collapse to exactly 1
//           fired() emission (trailing edge).
//   test 3: trigger() calls spaced > interval each emit one fired().
//   test 4: PropertiesDock.setImageInfo(x5 within 100ms) coalesces into a
//           single applyPendingInfo(); final label state reflects the LAST
//           call's data.
//

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include "Throttle.h"
#include "../src/media/docks/PropertiesDock.h"

using core::Throttle;
using docks::PropertiesDock;

namespace {

// Drive the event loop for `ms` milliseconds so QTimer::timeout can fire.
void pumpEvents(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

}  // namespace

class tst_F_O_Throttle : public QObject
{
    Q_OBJECT

private slots:
    void defaultIntervalIs200ms();
    void rapidTriggersCoalesceToOne();
    void spacedTriggersEmitEachTime();
    void propertiesDockSetInfoThrottled();
};

// 1) Default interval = 200ms; setInterval() is honored by the timer.
void tst_F_O_Throttle::defaultIntervalIs200ms()
{
    Throttle t;
    QCOMPARE(t.interval(), 200);

    Throttle t2(500);
    QCOMPARE(t2.interval(), 500);

    t2.setInterval(150);
    QCOMPARE(t2.interval(), 150);

    // After setInterval, a rapid trigger should still fire (with the new
    // interval). Just smoke-check: one trigger -> one fired within 300ms.
    QSignalSpy spy(&t2, &Throttle::fired);
    t2.trigger();
    QVERIFY(spy.wait(300));
    QCOMPARE(spy.count(), 1);
}

// 2) 3 rapid trigger() calls within 200ms -> exactly 1 fired().
void tst_F_O_Throttle::rapidTriggersCoalesceToOne()
{
    Throttle t(200);
    QSignalSpy spy(&t, &Throttle::fired);
    QCOMPARE(spy.count(), 0);

    t.trigger();
    t.trigger();
    t.trigger();
    // All within 200ms: timer keeps restarting, no fired yet.
    QCOMPARE(spy.count(), 0);

    QVERIFY(spy.wait(300));
    QCOMPARE(spy.count(), 1);

    // After fired, no further pending work.
    pumpEvents(50);
    QCOMPARE(spy.count(), 1);
}

// 3) Triggers > interval apart each emit one fired().
void tst_F_O_Throttle::spacedTriggersEmitEachTime()
{
    Throttle t(100);
    QSignalSpy spy(&t, &Throttle::fired);

    t.trigger();
    QVERIFY(spy.wait(300));
    QCOMPARE(spy.count(), 1);

    t.trigger();
    QVERIFY(spy.wait(300));
    QCOMPARE(spy.count(), 2);

    t.trigger();
    QVERIFY(spy.wait(300));
    QCOMPARE(spy.count(), 3);
}

// 4) PropertiesDock.setImageInfo: 5 rapid calls within 100ms coalesce to 1
//    apply; the last call's data is what gets shown.
void tst_F_O_Throttle::propertiesDockSetInfoThrottled()
{
    PropertiesDock dock;
    const int before = dock.appliedCount();

    for (int i = 0; i < 5; ++i) {
        PropertiesDock::ImageInfo info;
        info.filePath = QString("/tmp/img_%1.png").arg(i);
        info.width    = 100 + i;
        info.height   = 200 + i;
        info.format   = QString("PNG");
        info.dpi      = 72 + i;
        dock.setImageInfo(info);
    }

    // No apply yet: throttle window still open.
    QCOMPARE(dock.appliedCount(), before);

    // Wait > 200ms so the trailing-edge fired() runs.
    pumpEvents(300);

    // Exactly 1 apply coalesced 5 calls.
    QCOMPARE(dock.appliedCount(), before + 1);

    // Labels reflect the LAST call (i=4): width=104, height=204, path ends in img_4.png.
    const QString sizeTxt = dock.sizeLabelText();
    QVERIFY2(sizeTxt.contains(QString("104")),
             qPrintable("size label should contain '104', got: " + sizeTxt));
    QVERIFY2(sizeTxt.contains(QString("204")),
             qPrintable("size label should contain '204', got: " + sizeTxt));

    const QString pathTxt = dock.pathLabelText();
    QVERIFY2(pathTxt.contains(QString("img_4")),
             qPrintable("path label should contain 'img_4', got: " + pathTxt));
}

QTEST_MAIN(tst_F_O_Throttle)
#include "tst_F_O_Throttle.moc"
