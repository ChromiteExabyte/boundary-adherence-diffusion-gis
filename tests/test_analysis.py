"""Unit tests for src/analysis.py using in-memory numpy arrays.

No disk I/O or pre-generated sample data is required; all inputs are
constructed as numpy arrays inside pytest fixtures.
"""
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.analysis import boundary_adherence_curve

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def square_mask():
    """64×64 binary mask with a white square in the centre."""
    m = np.zeros((64, 64), dtype=np.uint8)
    m[16:48, 16:48] = 255
    return m


@pytest.fixture
def aligned_image():
    """RGB image whose bright region aligns exactly with the mask square."""
    img = np.zeros((64, 64, 3), dtype=np.uint8)
    img[16:48, 16:48] = 200
    return img


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_returns_three_values(square_mask, aligned_image):
    result = boundary_adherence_curve(square_mask, aligned_image, [1, 2, 3])
    assert len(result) == 3


def test_output_lengths_match_input_tolerances(square_mask, aligned_image):
    tolerances_in = [1, 2, 3, 4, 5]
    tols_out, f1_out, _ = boundary_adherence_curve(square_mask, aligned_image, tolerances_in)
    assert len(tols_out) == len(tolerances_in)
    assert len(f1_out) == len(tolerances_in)


def test_auc_is_float(square_mask, aligned_image):
    _, _, auc = boundary_adherence_curve(square_mask, aligned_image, [1, 3, 5])
    assert isinstance(auc, float)


def test_single_tolerance_yields_zero_auc(square_mask, aligned_image):
    _, _, auc = boundary_adherence_curve(square_mask, aligned_image, [3])
    assert auc == 0.0


def test_f1_scores_are_non_negative(square_mask, aligned_image):
    _, f1_scores, _ = boundary_adherence_curve(square_mask, aligned_image, [1, 2, 3, 4, 5])
    assert all(s >= 0.0 for s in f1_scores)


def test_larger_tolerance_improves_f1_for_aligned(square_mask, aligned_image):
    _, f1_scores, _ = boundary_adherence_curve(square_mask, aligned_image, [1, 2, 3, 4, 5])
    assert f1_scores[-1] >= f1_scores[0]


def test_auc_positive_for_aligned_image(square_mask, aligned_image):
    _, _, auc = boundary_adherence_curve(square_mask, aligned_image, [1, 3, 5])
    assert auc > 0.0
