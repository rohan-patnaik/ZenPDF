#include "PrintPolicy.h"

#include <QtTest>

class PrintPolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void boundsRenderDimensions();
    void rejectsHostilePageDimensions();
    void cancellationSkipsRender();
};

void PrintPolicyTest::boundsRenderDimensions() {
    const auto size = PrintPolicy::boundedRenderSize(QSizeF(612, 792), QSizeF(10'000, 10'000));
    QVERIFY(size.has_value());
    QVERIFY(size->width() <= PrintPolicy::maximumRenderDimension);
    QVERIFY(size->height() <= PrintPolicy::maximumRenderDimension);
    QVERIFY(size->width() > 0);
    QVERIFY(size->height() > 0);
}

void PrintPolicyTest::rejectsHostilePageDimensions() {
    QVERIFY(!PrintPolicy::boundedRenderSize(QSizeF(0, 792), QSizeF(1000, 1000)).has_value());
    QVERIFY(!PrintPolicy::boundedRenderSize(QSizeF(20'000, 792), QSizeF(1000, 1000)).has_value());
}

void PrintPolicyTest::cancellationSkipsRender() {
    int renderCalls = 0;
    const auto cancelled = PrintPolicy::renderIfNotCancelled(true, [&renderCalls] {
        ++renderCalls;
        return true;
    });
    QCOMPARE(static_cast<int>(cancelled), static_cast<int>(PrintRenderDecision::Cancelled));
    QCOMPARE(renderCalls, 0);

    const auto rendered = PrintPolicy::renderIfNotCancelled(false, [&renderCalls] {
        ++renderCalls;
        return true;
    });
    QCOMPARE(static_cast<int>(rendered), static_cast<int>(PrintRenderDecision::Rendered));
    QCOMPARE(renderCalls, 1);
}

QTEST_GUILESS_MAIN(PrintPolicyTest)
#include "PrintPolicyTest.moc"
