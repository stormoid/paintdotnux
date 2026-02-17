#include <gtest/gtest.h>
#include "data/selection.h"

#include <QPainterPath>
#include <QRect>
using namespace paintnux;

// ===== Basic Selection =====

TEST(Selection, InitiallyEmpty) {
    Selection sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_TRUE(sel.path().isEmpty());
    EXPECT_TRUE(sel.region().isEmpty());
}

TEST(Selection, SetPath) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(10, 10, 50, 50);
    sel.setPath(rect);

    EXPECT_FALSE(sel.isEmpty());
    EXPECT_FALSE(sel.path().isEmpty());
    EXPECT_FALSE(sel.region().isEmpty());
}

TEST(Selection, RegionContainsPoint) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(10, 10, 50, 50);
    sel.setPath(rect);

    EXPECT_TRUE(sel.region().contains(QPoint(25, 25)));
    EXPECT_FALSE(sel.region().contains(QPoint(0, 0)));
    EXPECT_FALSE(sel.region().contains(QPoint(100, 100)));
}

TEST(Selection, Reset) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(0, 0, 100, 100);
    sel.setPath(rect);
    EXPECT_FALSE(sel.isEmpty());

    sel.reset();
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_TRUE(sel.region().isEmpty());
}

TEST(Selection, SelectAll) {
    Selection sel;
    QRect bounds(0, 0, 200, 150);
    sel.selectAll(bounds);

    EXPECT_FALSE(sel.isEmpty());
    EXPECT_TRUE(sel.region().contains(QPoint(0, 0)));
    EXPECT_TRUE(sel.region().contains(QPoint(100, 75)));
    EXPECT_TRUE(sel.region().contains(QPoint(199, 149)));
}

TEST(Selection, Invert) {
    Selection sel;
    QRect bounds(0, 0, 100, 100);

    // Select a 50x50 rect in the corner
    QPainterPath rect;
    rect.addRect(0, 0, 50, 50);
    sel.setPath(rect);

    EXPECT_TRUE(sel.region().contains(QPoint(25, 25)));
    EXPECT_FALSE(sel.region().contains(QPoint(75, 75)));

    sel.invert(bounds);

    EXPECT_FALSE(sel.region().contains(QPoint(25, 25)));
    EXPECT_TRUE(sel.region().contains(QPoint(75, 75)));
}

TEST(Selection, InvertEmptyIsSelectAll) {
    Selection sel;
    QRect bounds(0, 0, 50, 50);
    sel.invert(bounds);

    // Inverting empty should select all
    EXPECT_FALSE(sel.isEmpty());
    EXPECT_TRUE(sel.region().contains(QPoint(0, 0)));
    EXPECT_TRUE(sel.region().contains(QPoint(49, 49)));
}

// ===== Continuation Pattern =====

TEST(SelectionContinuation, NoContinuationInitially) {
    Selection sel;
    EXPECT_FALSE(sel.hasContinuation());
}

TEST(SelectionContinuation, SetContinuation) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(0, 0, 50, 50);
    sel.setContinuation(rect, SelectionCombineMode::Replace);

    EXPECT_TRUE(sel.hasContinuation());
    // Base path should still be empty
    EXPECT_TRUE(sel.path().isEmpty());
    // Display path should show the continuation
    EXPECT_FALSE(sel.displayPath().isEmpty());
}

TEST(SelectionContinuation, CommitContinuation) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(0, 0, 50, 50);
    sel.setContinuation(rect, SelectionCombineMode::Replace);

    sel.commitContinuation();
    EXPECT_FALSE(sel.hasContinuation());
    EXPECT_FALSE(sel.path().isEmpty());
    EXPECT_FALSE(sel.isEmpty());
}

TEST(SelectionContinuation, ClearContinuation) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(0, 0, 50, 50);
    sel.setContinuation(rect, SelectionCombineMode::Replace);

    sel.clearContinuation();
    EXPECT_FALSE(sel.hasContinuation());
    EXPECT_TRUE(sel.isEmpty()); // base was empty, continuation cleared
}

TEST(SelectionContinuation, UnionMode) {
    Selection sel;
    // Set initial selection
    QPainterPath rect1;
    rect1.addRect(0, 0, 50, 50);
    sel.setPath(rect1);

    // Add continuation in Union mode
    QPainterPath rect2;
    rect2.addRect(25, 25, 50, 50);
    sel.setContinuation(rect2, SelectionCombineMode::Union);
    sel.commitContinuation();

    // Both regions should be selected
    EXPECT_TRUE(sel.region().contains(QPoint(10, 10)));  // from rect1
    EXPECT_TRUE(sel.region().contains(QPoint(60, 60)));  // from rect2
}

TEST(SelectionContinuation, ExcludeMode) {
    Selection sel;
    QPainterPath rect1;
    rect1.addRect(0, 0, 100, 100);
    sel.setPath(rect1);

    QPainterPath rect2;
    rect2.addRect(25, 25, 50, 50);
    sel.setContinuation(rect2, SelectionCombineMode::Exclude);
    sel.commitContinuation();

    EXPECT_TRUE(sel.region().contains(QPoint(10, 10)));   // outside subtracted area
    EXPECT_FALSE(sel.region().contains(QPoint(50, 50)));  // inside subtracted area
}

TEST(SelectionContinuation, IntersectMode) {
    Selection sel;
    QPainterPath rect1;
    rect1.addRect(0, 0, 100, 100);
    sel.setPath(rect1);

    QPainterPath rect2;
    rect2.addRect(50, 50, 100, 100);
    sel.setContinuation(rect2, SelectionCombineMode::Intersect);
    sel.commitContinuation();

    EXPECT_FALSE(sel.region().contains(QPoint(10, 10)));  // only in rect1
    EXPECT_TRUE(sel.region().contains(QPoint(75, 75)));   // in intersection
    EXPECT_FALSE(sel.region().contains(QPoint(120, 120)));// only in rect2
}

// ===== CombineMode from Modifiers =====

TEST(CombineModeModifiers, NoModifier) {
    auto mode = combineModeFromModifiers(Qt::NoModifier);
    EXPECT_EQ(mode, SelectionCombineMode::Replace);
}

TEST(CombineModeModifiers, Shift) {
    auto mode = combineModeFromModifiers(Qt::ShiftModifier);
    EXPECT_EQ(mode, SelectionCombineMode::Union);
}

TEST(CombineModeModifiers, Alt) {
    auto mode = combineModeFromModifiers(Qt::AltModifier);
    EXPECT_EQ(mode, SelectionCombineMode::Exclude);
}

TEST(CombineModeModifiers, Ctrl) {
    auto mode = combineModeFromModifiers(Qt::ControlModifier);
    EXPECT_EQ(mode, SelectionCombineMode::Xor);
}

TEST(CombineModeModifiers, CtrlShift) {
    auto mode = combineModeFromModifiers(Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(mode, SelectionCombineMode::Intersect);
}

TEST(CombineModeModifiers, CustomDefault) {
    auto mode = combineModeFromModifiers(Qt::NoModifier, SelectionCombineMode::Union);
    EXPECT_EQ(mode, SelectionCombineMode::Union);
}
