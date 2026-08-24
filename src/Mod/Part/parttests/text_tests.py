# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import unittest

import FreeCAD
import Part


def _find_test_font():
    """Return the path to a TTF/OTF font available on this system, or None."""
    candidates = [
        # Linux — DejaVu (very common)
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        # Windows
        r"C:\Windows\Fonts\arial.ttf",
        r"C:\Windows\Fonts\Arial.ttf",
        r"C:\Windows\Fonts\cour.ttf",
        # macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    ]
    for p in candidates:
        if os.path.isfile(p):
            return p
    return None


def _bbox(shapes):
    """Return (xmin, ymin, xmax, ymax) of the combined bounding box of all shapes."""
    xs, ys = [], []
    for s in shapes:
        bb = s.BoundBox
        xs += [bb.XMin, bb.XMax]
        ys += [bb.YMin, bb.YMax]
    return min(xs), min(ys), max(xs), max(ys)


FONT = _find_test_font()


@unittest.skipUnless(FONT, "No suitable font found on this system; skipping text wire tests")
class MakeTextWiresTests(unittest.TestCase):

    def test_ltr_default_nonempty(self):
        """LTR default produces at least one shape."""
        shapes = Part.makeTextWires("A", FONT, 10.0)
        self.assertGreater(len(shapes), 0)

    def test_ltr_two_chars_have_positive_width(self):
        """Two LTR characters produce a bounding box with positive X extent."""
        shapes = Part.makeTextWires("AB", FONT, 10.0)
        self.assertGreater(len(shapes), 0)
        xmin, _, xmax, _ = _bbox(shapes)
        self.assertGreater(xmax - xmin, 0.0)

    def test_rtl_nonempty(self):
        """RTL direction produces at least one shape."""
        shapes = Part.makeTextWires("AB", FONT, 10.0, 0.0, "rtl")
        self.assertGreater(len(shapes), 0)

    def test_rtl_similar_width_to_ltr(self):
        """RTL and LTR of the same two characters have approximately equal total width."""
        ltr = Part.makeTextWires("AB", FONT, 10.0, 0.0, "ltr")
        rtl = Part.makeTextWires("AB", FONT, 10.0, 0.0, "rtl")
        lx1, _, lx2, _ = _bbox(ltr)
        rx1, _, rx2, _ = _bbox(rtl)
        self.assertAlmostEqual(lx2 - lx1, rx2 - rx1, delta=0.5)

    def test_tracking_positive_widens_bbox(self):
        """Positive tracking produces a wider bounding box than zero tracking."""
        no_track = Part.makeTextWires("AB", FONT, 10.0, 0.0)
        with_track = Part.makeTextWires("AB", FONT, 10.0, 3.0)
        x1a, _, x2a, _ = _bbox(no_track)
        x1b, _, x2b, _ = _bbox(with_track)
        self.assertGreater(x2b - x1b, x2a - x1a)

    def test_ttb_taller_than_wide(self):
        """TTB direction stacks two characters vertically: height > width."""
        shapes = Part.makeTextWires("AB", FONT, 10.0, 0.0, "ttb")
        self.assertGreater(len(shapes), 0)
        xmin, ymin, xmax, ymax = _bbox(shapes)
        self.assertGreater(ymax - ymin, xmax - xmin)

    def test_backward_compat_defaults_match_explicit_ltr(self):
        """Calling with no direction/tracking gives the same result as explicit ltr/0."""
        default_shapes = Part.makeTextWires("A", FONT, 10.0)
        explicit_shapes = Part.makeTextWires("A", FONT, 10.0, 0.0, "ltr")
        self.assertEqual(len(default_shapes), len(explicit_shapes))
        xd1, yd1, xd2, yd2 = _bbox(default_shapes)
        xe1, ye1, xe2, ye2 = _bbox(explicit_shapes)
        self.assertAlmostEqual(xd2 - xd1, xe2 - xe1, places=6)
        self.assertAlmostEqual(yd2 - yd1, ye2 - ye1, places=6)

    def test_btt_nonempty(self):
        """BTT direction produces at least one shape."""
        shapes = Part.makeTextWires("A", FONT, 10.0, 0.0, "btt")
        self.assertGreater(len(shapes), 0)
