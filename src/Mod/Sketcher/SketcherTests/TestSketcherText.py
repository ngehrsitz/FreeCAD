# SPDX-License-Identifier: LGPL-2.1-or-later

# **************************************************************************
#   Copyright (c) 2024 FreeCAD contributors                               *
#                                                                         *
#   This file is part of the FreeCAD CAx development system.              *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU Lesser General Public License (LGPL)    *
#   as published by the Free Software Foundation; either version 2 of     *
#   the License, or (at your option) any later version.                   *
#   for detail see the LICENCE text file.                                 *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful,            *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU Library General Public License for more details.                  *
#                                                                         *
#   You should have received a copy of the GNU Library General Public     *
#   License along with FreeCAD; if not, write to the Free Software        *
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#   USA                                                                   *
# **************************************************************************

"""Phase 1 tests for the Sketcher CreateText rectangle format (CreateText-v2)."""

import os
import tempfile
import unittest
import FreeCAD
import Part
import Sketcher
from Part import Precision

App = FreeCAD

V = FreeCAD.Vector


def _new_sketch(doc):
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    return sk


def _build_rect_frame(sk, ox=0.0, oy=0.0, width=10.0, aspect=0.6):
    """Add 4-line rectangle frame + structural constraints.

    Returns (uId, vId, l3Id, l4Id).  All lines are construction.
    Frame layout::

        vLine.end ── l3 ── l3.end
           |                 |
        vLine             l4
           |                 |
        uLine.start ── uLine ── uLine.end
          == vLine.start        == l4.start
    """
    h = width * aspect
    i = sk.GeometryCount

    # uLine: bottom (width direction)  start→end = left→right
    sk.addGeometry(Part.LineSegment(V(ox, oy, 0), V(ox + width, oy, 0)), True)
    # vLine: left (height direction)   start→end = bottom→top
    sk.addGeometry(Part.LineSegment(V(ox, oy, 0), V(ox, oy + h, 0)), True)
    # l3: top                          start→end = left→right
    sk.addGeometry(Part.LineSegment(V(ox, oy + h, 0), V(ox + width, oy + h, 0)), True)
    # l4: right                        start→end = bottom→top
    sk.addGeometry(Part.LineSegment(V(ox + width, oy, 0), V(ox + width, oy + h, 0)), True)

    uId, vId, l3Id, l4Id = i, i + 1, i + 2, i + 3

    # Coincident constraints: join the four corners
    sk.addConstraint(Sketcher.Constraint("Coincident", uId, 1, vId, 1))    # origin
    sk.addConstraint(Sketcher.Constraint("Coincident", uId, 2, l4Id, 1))   # bottom-right
    sk.addConstraint(Sketcher.Constraint("Coincident", vId, 2, l3Id, 1))   # top-left
    sk.addConstraint(Sketcher.Constraint("Coincident", l3Id, 2, l4Id, 2))  # top-right

    # Equal + Parallel: make opposite sides equal
    sk.addConstraint(Sketcher.Constraint("Equal", uId, l3Id))
    sk.addConstraint(Sketcher.Constraint("Equal", vId, l4Id))
    sk.addConstraint(Sketcher.Constraint("Parallel", uId, l3Id))
    sk.addConstraint(Sketcher.Constraint("Parallel", vId, l4Id))

    return uId, vId, l3Id, l4Id


def _fix_frame(sk, uId):
    """Add anchor (origin) + horizontal angle constraints to the uLine."""
    sk.addConstraint(Sketcher.Constraint("DistanceX", uId, 1, 0.0))
    sk.addConstraint(Sketcher.Constraint("DistanceY", uId, 1, 0.0))
    sk.addConstraint(Sketcher.Constraint("Horizontal", uId))


class TestTextConstraintMetadata(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("TestTextMeta")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def test_rect_format_no_isTextHeight(self):
        """Text constraint created with element list has no isTextHeight key."""
        sk = _new_sketch(self.doc)
        uId, vId, l3Id, l4Id = _build_rect_frame(sk)
        sk.addConstraint(Sketcher.Constraint("Text", [uId, vId, l3Id, l4Id], "Hi", ""))
        vals = sk.Constraints
        tc = next((vals[i] for i in range(len(vals)) if vals[i].Type == "Text"), None)
        self.assertIsNotNone(tc, "No Text constraint found")
        self.assertFalse(
            tc.hasIsTextHeight(),
            "Rectangle format Text constraint must not contain 'isTextHeight' metadata key",
        )

    def test_legacy_format_has_isTextHeight(self):
        """Text constraint created with isHeight=True carries the isTextHeight key."""
        sk = _new_sketch(self.doc)
        i = sk.GeometryCount
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), True)
        sk.addConstraint(Sketcher.Constraint("Text", i, 0, 0, "Hi", "", True))
        vals = sk.Constraints
        tc = next((vals[j] for j in range(len(vals)) if vals[j].Type == "Text"), None)
        self.assertIsNotNone(tc, "No Text constraint found")
        self.assertTrue(
            tc.hasIsTextHeight(),
            "Legacy Text constraint must carry 'isTextHeight' metadata key",
        )

    def test_text_aspect_ratio_value_stored(self):
        """TextAspectRatio constraint stores ratio in Value field."""
        sk = _new_sketch(self.doc)
        uId, vId, l3Id, l4Id = _build_rect_frame(sk, aspect=0.75)
        ar_idx = sk.addConstraint(
            Sketcher.Constraint("TextAspectRatio", uId, vId, 0.75)
        )
        c = sk.Constraints[ar_idx]
        self.assertEqual(c.Type, "TextAspectRatio")
        self.assertAlmostEqual(
            c.Value, 0.75, delta=Precision.confusion(), msg="Ratio must round-trip via Value"
        )

    def test_text_aspect_ratio_first_second(self):
        """TextAspectRatio constraint First/Second point to uLine and vLine."""
        sk = _new_sketch(self.doc)
        uId, vId, l3Id, l4Id = _build_rect_frame(sk)
        ar_idx = sk.addConstraint(Sketcher.Constraint("TextAspectRatio", uId, vId, 0.6))
        c = sk.Constraints[ar_idx]
        self.assertEqual(c.First, uId)
        self.assertEqual(c.Second, vId)


class TestTextFrameDoF(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("TestTextDoF")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def test_frame_plus_aspect_lock_has_3_dof(self):
        """Rectangle frame + aspect lock alone has 3 free DoF (anchor + angle still free)."""
        sk = _new_sketch(self.doc)
        aspect = 0.6
        uId, vId, l3Id, l4Id = _build_rect_frame(sk, aspect=aspect)
        sk.addConstraint(Sketcher.Constraint("TextAspectRatio", uId, vId, aspect))
        sk.addConstraint(Sketcher.Constraint("Text", [uId, vId, l3Id, l4Id], "A", ""))
        sk.solve()
        self.assertEqual(
            sk.DoF,
            3,
            "Rectangle frame with aspect lock should have exactly 3 DoF (translation x2, rotation x1)",
        )

    def test_frame_fully_constrained_dof_zero(self):
        """Frame + aspect lock + anchor + angle is fully constrained (DoF = 0)."""
        sk = _new_sketch(self.doc)
        aspect = 0.6
        uId, vId, l3Id, l4Id = _build_rect_frame(sk, aspect=aspect)
        sk.addConstraint(Sketcher.Constraint("TextAspectRatio", uId, vId, aspect))
        sk.addConstraint(Sketcher.Constraint("Text", [uId, vId, l3Id, l4Id], "A", ""))
        _fix_frame(sk, uId)
        sk.addConstraint(Sketcher.Constraint("Distance", vId, 6.0))  # one size dim
        status = sk.solve()
        self.assertEqual(status, 0, "Solver must converge without conflicts")
        self.assertEqual(sk.DoF, 0, "Fully-constrained text frame must have DoF = 0")
        self.assertEqual(
            len(sk.Redundancies), 0, "No redundant constraints expected when fully constrained"
        )

    def test_two_dimensions_without_aspect_lock(self):
        """Without aspect lock, width and height are independent; two dims → DoF = 0."""
        sk = _new_sketch(self.doc)
        aspect = 0.6
        uId, vId, l3Id, l4Id = _build_rect_frame(sk, aspect=aspect)
        # Deliberately no TextAspectRatio
        sk.addConstraint(Sketcher.Constraint("Text", [uId, vId, l3Id, l4Id], "A", ""))
        _fix_frame(sk, uId)
        sk.addConstraint(Sketcher.Constraint("Distance", uId, 10.0))
        sk.addConstraint(Sketcher.Constraint("Distance", vId, 6.0))
        status = sk.solve()
        self.assertEqual(status, 0, "Solver must converge with two independent dimensions")
        self.assertEqual(sk.DoF, 0, "Two independent dims must fully constrain the frame")
        self.assertEqual(
            len(sk.Redundancies),
            0,
            "No redundancy expected when both dims are set without aspect lock",
        )

    def test_two_dimensions_with_aspect_lock_redundant(self):
        """With aspect lock, adding both width and height produces a redundant constraint."""
        sk = _new_sketch(self.doc)
        aspect = 0.6
        uId, vId, l3Id, l4Id = _build_rect_frame(sk, aspect=aspect)
        sk.addConstraint(Sketcher.Constraint("TextAspectRatio", uId, vId, aspect))
        sk.addConstraint(Sketcher.Constraint("Text", [uId, vId, l3Id, l4Id], "A", ""))
        _fix_frame(sk, uId)
        sk.addConstraint(Sketcher.Constraint("Distance", uId, 10.0))
        sk.addConstraint(Sketcher.Constraint("Distance", vId, 6.0))  # conflicts with aspect lock
        sk.solve()
        self.assertGreater(
            len(sk.Redundancies),
            0,
            "Aspect lock + explicit width + explicit height must produce at least one redundant constraint",
        )


class TestTextMigration(unittest.TestCase):
    """Tests for migration of legacy single-line Text constraints on file load."""

    def setUp(self):
        self.doc = App.newDocument("TestTextMigration")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def test_migration_removes_isTextHeight(self):
        """After save/reload, a legacy Text constraint (isTextHeight) is migrated to rectangle format."""
        sk = _new_sketch(self.doc)
        i = sk.GeometryCount
        # Build legacy geometry: a single line as the handle
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 0, 0)), True)
        # Add anchor constraints so the sketch is mostly constrained
        sk.addConstraint(Sketcher.Constraint("DistanceX", i, 1, 0.0))
        sk.addConstraint(Sketcher.Constraint("DistanceY", i, 1, 0.0))
        sk.addConstraint(Sketcher.Constraint("Horizontal", i))
        sk.addConstraint(Sketcher.Constraint("Distance", i, 10.0))
        # Add a legacy Text constraint (isHeight=True marks it as legacy)
        sk.addConstraint(Sketcher.Constraint("Text", i, 0, 0, "A", "", True))

        # Save and reload to trigger onSketchRestore → migrateLegacyTextConstraints
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "text_migration_test.FCStd")
            self.doc.saveAs(path)
            App.closeDocument(self.doc.Name)

            doc2 = App.openDocument(path)
            try:
                sk2 = doc2.Sketch
                vals = sk2.Constraints
                tc = next(
                    (vals[j] for j in range(len(vals)) if vals[j].Type == "Text"), None
                )
                self.assertIsNotNone(tc, "Text constraint must still exist after migration")
                self.assertFalse(
                    tc.hasIsTextHeight(),
                    "After migration, isTextHeight must be absent from metadata",
                )
                # The sketch should now have at least 4 line segments (1 original + 3 added)
                geo_lines = [
                    g
                    for g in [sk2.getGeometry(j) for j in range(sk2.GeometryCount)]
                    if isinstance(g, Part.LineSegment)
                ]
                self.assertGreaterEqual(
                    len(geo_lines),
                    4,
                    "Migration must add 3 extra lines to form the 4-line rectangle frame",
                )
            finally:
                App.closeDocument(doc2.Name)
                self.doc = App.newDocument("TestTextMigration")  # restore for tearDown


if __name__ == "__main__":
    unittest.main()
