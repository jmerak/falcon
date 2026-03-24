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
// Implements Π_DPClip for Falcon VFL-MLP:
//   per-sample L2 clipping + Gaussian noise injection at the split layer.
//

#include <falcon/algorithm/vertical/nn/dp_clip.h>
#include <falcon/algorithm/vertical/nn/mlp.h>
#include <falcon/common.h>
#include <falcon/operator/conversion/op_conv.h>
#include <falcon/utils/logger/logger.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <random>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Pure-math helpers (no Party / SPDZ dependency – directly unit-testable)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<double>
compute_clip_factors(const std::vector<std::vector<double>> &shares_mat,
                     double clip_threshold) {
  int batch_size = static_cast<int>(shares_mat.size());
  std::vector<double> factors(batch_size, 1.0);
  for (int i = 0; i < batch_size; i++) {
    double sq_norm = 0.0;
    for (double v : shares_mat[i]) {
      sq_norm += v * v;
    }
    double norm = std::sqrt(sq_norm);
    if (norm > clip_threshold && norm > 0.0) {
      factors[i] = clip_threshold / norm;
    }
  }
  return factors;
}

void clip_shares_mat_inplace(std::vector<std::vector<double>> &shares_mat,
                             double clip_threshold) {
  std::vector<double> factors = compute_clip_factors(shares_mat, clip_threshold);
  int batch_size = static_cast<int>(shares_mat.size());
  int n_outputs  = (batch_size > 0) ? static_cast<int>(shares_mat[0].size()) : 0;
  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < n_outputs; j++) {
      shares_mat[i][j] *= factors[i];
    }
  }
}

void add_gaussian_noise_to_shares(std::vector<std::vector<double>> &shares_mat,
                                  double noise_sigma_per_party) {
  if (noise_sigma_per_party <= 0.0) {
    return;
  }
  // Use a thread-local PRNG to avoid contention in multi-threaded contexts.
  static thread_local std::mt19937 gen(std::random_device{}());
  std::normal_distribution<double> dist(0.0, noise_sigma_per_party);

  int batch_size = static_cast<int>(shares_mat.size());
  int n_outputs  = (batch_size > 0) ? static_cast<int>(shares_mat[0].size()) : 0;
  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < n_outputs; j++) {
      shares_mat[i][j] += dist(gen);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Full Π_DPClip protocol
// ─────────────────────────────────────────────────────────────────────────────

void dp_clip_layer0_outputs(const Party &party, EncodedNumber **cipher_mat,
                            EncodedNumber **result_mat, int batch_size,
                            int n_outputs, double clip_threshold,
                            double noise_sigma_dp) {
  log_info("[dp_clip_layer0_outputs] Π_DPClip: "
           "batch_size=" + std::to_string(batch_size) +
           " n_outputs=" + std::to_string(n_outputs) +
           " clip_threshold=" + std::to_string(clip_threshold) +
           " noise_sigma_dp=" + std::to_string(noise_sigma_dp));

  // ── Step 1: Π_C2S – cipher matrix → secret shares ────────────────────────
  int cipher_precision = std::abs(cipher_mat[0][0].getter_exponent());
  std::vector<std::vector<double>> shares_mat;
  ciphers_mat_to_secret_shares_mat(party, cipher_mat, shares_mat,
                                   batch_size, n_outputs,
                                   ACTIVE_PARTY_ID, cipher_precision);
  log_info("[dp_clip_layer0_outputs] Step 1 (Π_C2S) done. "
           "shares_mat.rows=" + std::to_string(shares_mat.size()));

  // ── Step 2: SPDZ per-sample L2 clipping (DP_CLIP_L2) ─────────────────────
  // Pack local shares into a flat vector and send to SPDZ.
  // The SPDZ program computes:
  //   for each sample i: s_i = min(1, C / ||z_i||_2),
  //   returns  s_i * z_i  as new share vectors to each party.
  // Public values sent: [batch_size, n_outputs, C encoded as Q16 fixed-point]
  // Private values sent: flat shares (batch_size * n_outputs doubles)
  std::vector<double> flat_shares;
  flat_shares.reserve(batch_size * n_outputs);
  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < n_outputs; j++) {
      flat_shares.push_back(shares_mat[i][j]);
    }
  }

  // Encode C as a Q16 fixed-point integer so the SPDZ program can read it.
  int clip_int = static_cast<int>(clip_threshold * (1 << 16));

  std::vector<int> public_values;
  public_values.push_back(batch_size);
  public_values.push_back(n_outputs);
  public_values.push_back(clip_int);

  falcon::SpdzMlpCompType comp_type = falcon::DP_CLIP_L2;

  std::promise<std::vector<double>> promise_vals;
  std::future<std::vector<double>> future_vals = promise_vals.get_future();
  std::thread spdz_clip_thread(
      spdz_mlp_computation,
      party.party_num, party.party_id,
      party.executor_mpc_ports, party.host_names,
      static_cast<int>(public_values.size()), public_values,
      static_cast<int>(flat_shares.size()), flat_shares,
      comp_type, &promise_vals);

  std::vector<double> clipped_flat = future_vals.get();
  spdz_clip_thread.join();
  log_info("[dp_clip_layer0_outputs] Step 2 (SPDZ DP_CLIP_L2) done. "
           "returned=" + std::to_string(clipped_flat.size()));

  // Reshape flat result back to (batch_size × n_outputs).
  std::vector<std::vector<double>> clipped_shares(
      batch_size, std::vector<double>(n_outputs, 0.0));
  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < n_outputs; j++) {
      clipped_shares[i][j] = clipped_flat[i * n_outputs + j];
    }
  }

  // ── Step 3: Local Gaussian noise injection ────────────────────────────────
  // Each party j adds N(0, (σ_dp/√m)² I) independently to its local share.
  // By Gaussian additivity: Σ_j N(0, σ_dp²/m) = N(0, σ_dp²).
  double sigma_j = noise_sigma_dp /
                   std::sqrt(static_cast<double>(party.party_num));
  add_gaussian_noise_to_shares(clipped_shares, sigma_j);
  log_info("[dp_clip_layer0_outputs] Step 3 (local noise, σ_j=" +
           std::to_string(sigma_j) + ") done.");

  // ── Step 4: Π_S2C – clipped+noised shares → cipher matrix ────────────────
  for (int i = 0; i < batch_size; i++) {
    secret_shares_to_ciphers(party, result_mat[i],
                             clipped_shares[i], n_outputs,
                             ACTIVE_PARTY_ID, cipher_precision);
  }
  log_info("[dp_clip_layer0_outputs] Step 4 (Π_S2C) done. Π_DPClip complete.");
}
