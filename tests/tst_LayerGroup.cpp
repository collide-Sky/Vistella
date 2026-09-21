// SPDX-License-Identifier: MIT
//
// tst_LayerGroup.cpp - P1.5.2 (2026-09-21)
//
//   Tests LayerStack::mergeIntoGroup + flattenGroup (PS-style group/ungroup).
//
//   Verifies:
//     - mergeIntoGroup with >= 2 adjacent layers packs into 1 Group container
//     - non-adjacent / < 2 indices fail with -1
//     - flattenGroup restores children in order
//     - LayerKind::Group isValid() returns true
//     - render() with Group layer returns valid output (composite of children)
//
#include <QtTest/QtTest>
#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/Layer.h"

#include <opencv2/core.hpp>

#include <QCoreApplication>

using namespace layers;

class tst_LayerGroup : public QObject
{
    Q_OBJECT

private:
    static Layer makeBitmapLayer(const QString& name, const cv::Scalar& color, int idx)
    {
        Layer l;
        l.kind = Layer::Bitmap;
        l.name = name;
        l.image = cv::Mat(50, 50, CV_8UC3, color);
        l.zOrder = idx;
        return l;
    }

    cv::Mat renderOrEmpty(LayerStack* s)
    {
        return s->render();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QCoreApplication::instance() != nullptr);
    }

    void test_merge_into_group_basic()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        s.addLayer(makeBitmapLayer("B", cv::Scalar(0, 255, 0), 1));
        s.addLayer(makeBitmapLayer("C", cv::Scalar(0, 0, 255), 2));
        QCOMPARE(s.count(), 3);

        const int groupIdx = s.mergeIntoGroup({0, 1, 2});
        QVERIFY(groupIdx >= 0);
        QCOMPARE(s.count(), 1);

        auto grp = s.at(groupIdx);
        QVERIFY(grp);
        QCOMPARE(int(grp->kind), int(Layer::Group));
        const auto& children = s.groupChildrenOf(groupIdx);
        QCOMPARE(int(children.size()), 3);
        QCOMPARE(children[0]->name, QStringLiteral("A"));
        QCOMPARE(children[1]->name, QStringLiteral("B"));
        QCOMPARE(children[2]->name, QStringLiteral("C"));
    }

    void test_merge_too_few_fails()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        s.addLayer(makeBitmapLayer("B", cv::Scalar(0, 255, 0), 1));

        // Empty list -> -1
        QCOMPARE(s.mergeIntoGroup({}), -1);
        // Single index -> -1
        QCOMPARE(s.mergeIntoGroup({0}), -1);
        // Out of bounds -> -1
        QCOMPARE(s.mergeIntoGroup({0, 5}), -1);
        // Non-adjacent -> -1
        s.addLayer(makeBitmapLayer("C", cv::Scalar(0, 0, 255), 2));
        QCOMPARE(s.mergeIntoGroup({0, 2}), -1);
        // Adjacent still works (returns new Group index at end of stack)
        QCOMPARE(s.mergeIntoGroup({1, 2}), 1);   // returns idx 1 (tail position)
    }

    void test_flatten_group_restores_children()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        s.addLayer(makeBitmapLayer("B", cv::Scalar(0, 255, 0), 1));
        s.addLayer(makeBitmapLayer("C", cv::Scalar(0, 0, 255), 2));

        const int groupIdx = s.mergeIntoGroup({0, 1, 2});
        QCOMPARE(s.count(), 1);

        const int restored = s.flattenGroup(groupIdx);
        QCOMPARE(restored, 3);
        QCOMPARE(s.count(), 3);

        // Verify names in order
        QCOMPARE(s.at(0)->name, QStringLiteral("A"));
        QCOMPARE(s.at(1)->name, QStringLiteral("B"));
        QCOMPARE(s.at(2)->name, QStringLiteral("C"));
    }

    void test_flatten_non_group_fails()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        QCOMPARE(s.flattenGroup(0), -1);   // 0 is Bitmap, not Group
    }

    void test_group_layer_is_valid()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        s.addLayer(makeBitmapLayer("B", cv::Scalar(0, 255, 0), 1));
        const int groupIdx = s.mergeIntoGroup({0, 1});
        auto grp = s.at(groupIdx);
        QVERIFY(grp->isValid());
    }

    void test_group_render_produces_output()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("Base", cv::Scalar(255, 255, 255), 0));
        s.addLayer(makeBitmapLayer("Red", cv::Scalar(0, 0, 255), 1));
        s.addLayer(makeBitmapLayer("Blue", cv::Scalar(255, 0, 0), 2));
        const int groupIdx = s.mergeIntoGroup({1, 2});
        QCOMPARE(s.count(), 2);

        cv::Mat out = renderOrEmpty(&s);
        QVERIFY(!out.empty());
        QCOMPARE(out.rows, 50);
        QCOMPARE(out.cols, 50);
    }

    void test_merge_into_group_preserves_cv_mat()
    {
        LayerStack s;
        s.addLayer(makeBitmapLayer("A", cv::Scalar(255, 0, 0), 0));
        s.addLayer(makeBitmapLayer("B", cv::Scalar(0, 255, 0), 1));
        const int groupIdx = s.mergeIntoGroup({0, 1});

        const auto& children = s.groupChildrenOf(groupIdx);
        QVERIFY(!children[0]->image.empty());
        QCOMPARE(children[0]->image.rows, 50);
        QCOMPARE(children[0]->image.cols, 50);
        QCOMPARE(children[1]->image.rows, 50);
    }
};

QTEST_MAIN(tst_LayerGroup)
#include "tst_LayerGroup.moc"