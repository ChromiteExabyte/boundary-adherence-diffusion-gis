"""Unit tests for src/metrics.py using in-memory numpy arrays.

No disk I/O or pre-generated sample data is required; all inputs are
constructed as numpy arrays inside pytest fixtures.
"""
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.metrics import calculate_boundary_adherence

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

RESULT_KEYS = {"boundary_precision", "boundary_recall", "boundary_f1", "tolerance_used"}


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


@pytest.fixture
def offset_image():
    """RGB image shifted 2 px from the mask position."""
    img = np.zeros((64, 64, 3), dtype=np.uint8)
    img[18:50, 18:50] = 200
    return img


@pytest.fixture
def blank_image():
    """All-zero RGB image (no features)."""
    return np.zeros((64, 64, 3), dtype=np.uint8)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_returns_expected_keys(square_mask, aligned_image):
    result = calculate_boundary_adherence(square_mask, aligned_image)
    assert set(result.keys()) == RESULT_KEYS


def test_tolerance_recorded_in_result(square_mask, aligned_image):
    result = calculate_boundary_adherence(square_mask, aligned_image, tolerance_px=4)
    assert result["tolerance_used"] == 4


def test_aligned_image_high_f1(square_mask, aligned_image):
    result = calculate_boundary_adherence(square_mask, aligned_image, tolerance_px=3)
    assert result["boundary_f1"] > 0.9


def test_offset_image_lower_f1_than_aligned(square_mask, aligned_image, offset_image):
    aligned = calculate_boundary_adherence(square_mask, aligned_image, tolerance_px=1)
    offset = calculate_boundary_adherence(square_mask, offset_image, tolerance_px=1)
    assert offset["boundary_f1"] < aligned["boundary_f1"]


def test_blank_image_near_zero_scores(square_mask, blank_image):
    result = calculate_boundary_adherence(square_mask, blank_image, tolerance_px=3)
    assert result["boundary_precision"] < 0.05
    assert result["boundary_recall"] < 0.05
    assert result["boundary_f1"] < 0.05


def test_scores_are_floats(square_mask, aligned_image):
    result = calculate_boundary_adherence(square_mask, aligned_image)
    assert isinstance(result["boundary_precision"], float)
    assert isinstance(result["boundary_recall"], float)
    assert isinstance(result["boundary_f1"], float)


def test_scores_bounded_between_zero_and_one(square_mask, aligned_image):
    result = calculate_boundary_adherence(square_mask, aligned_image)
    for key in ("boundary_precision", "boundary_recall", "boundary_f1"):
        assert 0.0 <= result[key] <= 1.0


def test_larger_tolerance_improves_f1_for_offset(square_mask, offset_image):
    tight = calculate_boundary_adherence(square_mask, offset_image, tolerance_px=1)
    loose = calculate_boundary_adherence(square_mask, offset_image, tolerance_px=5)
    assert loose["boundary_f1"] >= tight["boundary_f1"]
