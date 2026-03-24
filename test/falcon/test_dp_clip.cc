/**
MIT License

Copyright (c) 2020 lemonviv

    Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//
// Unit tests for Π_DPClip pure-math helpers.
// These tests cover compute_clip_factors, clip_shares_mat_inplace, and
// add_gaussian_noise_to_shares, which require no Party / SPDZ setup.
//

#include <falcon/algorithm/vertical/nn/dp_clip.h>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// compute_clip_factors
// ─────────────────────────────────────────────────────────────────────────────

TEST(DPClip, ComputeClipFactors_AllBelowThreshold) {
  // All rows have L2 norm < C → all factors should be 1.0
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},   // norm = 5.0
      {1.0, 0.0},   // norm = 1.0
      {0.0, 0.0},   // norm = 0.0
  };
  double C = 10.0;
  auto factors = compute_clip_factors(mat, C);
  ASSERT_EQ(factors.size(), 3u);
  EXPECT_NEAR(factors[0], 1.0, 1e-9);
  EXPECT_NEAR(factors[1], 1.0, 1e-9);
  EXPECT_NEAR(factors[2], 1.0, 1e-9);
}

TEST(DPClip, ComputeClipFactors_AboveThreshold) {
  // Row with norm 5.0 and C = 2.5 → factor = 0.5
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},  // norm = 5.0
  };
  double C = 2.5;
  auto factors = compute_clip_factors(mat, C);
  ASSERT_EQ(factors.size(), 1u);
  EXPECT_NEAR(factors[0], 0.5, 1e-9);
}

TEST(DPClip, ComputeClipFactors_ExactlyAtThreshold) {
  // norm == C → factor must be exactly 1.0
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},  // norm = 5.0
  };
  double C = 5.0;
  auto factors = compute_clip_factors(mat, C);
  EXPECT_NEAR(factors[0], 1.0, 1e-9);
}

TEST(DPClip, ComputeClipFactors_Mixed) {
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},   // norm = 5.0 > C=3 → factor = 0.6
      {1.0, 0.0},   // norm = 1.0 < C=3 → factor = 1.0
      {0.0, 6.0},   // norm = 6.0 > C=3 → factor = 0.5
  };
  double C = 3.0;
  auto factors = compute_clip_factors(mat, C);
  ASSERT_EQ(factors.size(), 3u);
  EXPECT_NEAR(factors[0], 0.6, 1e-9);
  EXPECT_NEAR(factors[1], 1.0, 1e-9);
  EXPECT_NEAR(factors[2], 0.5, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// clip_shares_mat_inplace
// ─────────────────────────────────────────────────────────────────────────────

TEST(DPClip, ClipSharesMatInplace_NormsRespectThreshold) {
  // After clipping, every row's L2 norm must be <= C.
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},   // norm = 5.0 > C=2.5 → clipped
      {1.0, 0.0},   // norm = 1.0 < C=2.5 → unchanged
      {6.0, 8.0},   // norm = 10.0 > C=2.5 → clipped
  };
  double C = 2.5;
  clip_shares_mat_inplace(mat, C);

  for (const auto &row : mat) {
    double sq_norm = 0.0;
    for (double v : row) sq_norm += v * v;
    EXPECT_LE(std::sqrt(sq_norm), C + 1e-9);
  }
}

TEST(DPClip, ClipSharesMatInplace_DirectionPreserved) {
  // Clip should scale the vector without changing direction.
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},  // unit direction: (0.6, 0.8); clipped to C=2.5 → (1.5, 2.0)
  };
  double C = 2.5;
  clip_shares_mat_inplace(mat, C);
  EXPECT_NEAR(mat[0][0], 1.5, 1e-9);
  EXPECT_NEAR(mat[0][1], 2.0, 1e-9);
}

TEST(DPClip, ClipSharesMatInplace_BelowThresholdUnchanged) {
  // Rows already within threshold must not be altered.
  std::vector<std::vector<double>> mat = {
      {1.0, 0.0},  // norm 1 < C=5
      {2.0, 3.0},  // norm ~3.6 < C=5
  };
  double C = 5.0;
  auto original = mat;
  clip_shares_mat_inplace(mat, C);
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      EXPECT_NEAR(mat[i][j], original[i][j], 1e-9);
    }
  }
}

TEST(DPClip, ClipSharesMatInplace_ZeroVectorUnchanged) {
  // A zero vector has norm 0; clipping should not produce NaN or Inf.
  std::vector<std::vector<double>> mat = {
      {0.0, 0.0},
  };
  clip_shares_mat_inplace(mat, 1.0);
  EXPECT_NEAR(mat[0][0], 0.0, 1e-9);
  EXPECT_NEAR(mat[0][1], 0.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_gaussian_noise_to_shares
// ─────────────────────────────────────────────────────────────────────────────

TEST(DPClip, AddGaussianNoise_ZeroSigmaNoChange) {
  // sigma = 0 → matrix must remain identical.
  std::vector<std::vector<double>> mat = {
      {1.0, 2.0, 3.0},
      {4.0, 5.0, 6.0},
  };
  auto original = mat;
  add_gaussian_noise_to_shares(mat, 0.0);
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(mat[i][j], original[i][j]);
    }
  }
}

TEST(DPClip, AddGaussianNoise_PositiveSigmaModifiesMatrix) {
  // With sigma > 0, at least one element should change.
  std::vector<std::vector<double>> mat = {
      {1.0, 2.0},
      {3.0, 4.0},
  };
  auto original = mat;
  add_gaussian_noise_to_shares(mat, 1.0);

  bool any_changed = false;
  for (int i = 0; i < 2 && !any_changed; i++) {
    for (int j = 0; j < 2 && !any_changed; j++) {
      if (mat[i][j] != original[i][j]) {
        any_changed = true;
      }
    }
  }
  EXPECT_TRUE(any_changed);
}

TEST(DPClip, AddGaussianNoise_NegativeSigmaSkipped) {
  // Negative sigma is treated as <= 0 → no change.
  std::vector<std::vector<double>> mat = {
      {5.0, 6.0},
  };
  auto original = mat;
  add_gaussian_noise_to_shares(mat, -1.0);
  EXPECT_EQ(mat[0][0], original[0][0]);
  EXPECT_EQ(mat[0][1], original[0][1]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Combined: clip then noise
// ─────────────────────────────────────────────────────────────────────────────

TEST(DPClip, ClipThenNoise_ResultNormMayExceedC) {
  // After clipping the norm is <= C, but after adding noise it may exceed C.
  // This is expected: noise is added on top of the clipped vector.
  std::vector<std::vector<double>> mat = {
      {3.0, 4.0},  // norm 5 > C=2.5 → clipped to norm 2.5
  };
  clip_shares_mat_inplace(mat, 2.5);
  // Verify post-clip norm
  double norm_after_clip =
      std::sqrt(mat[0][0] * mat[0][0] + mat[0][1] * mat[0][1]);
  EXPECT_NEAR(norm_after_clip, 2.5, 1e-9);

  // Add substantial noise and verify matrix was modified
  auto before_noise = mat;
  add_gaussian_noise_to_shares(mat, 10.0);
  bool changed = (mat[0][0] != before_noise[0][0]) ||
                 (mat[0][1] != before_noise[0][1]);
  EXPECT_TRUE(changed);
}
