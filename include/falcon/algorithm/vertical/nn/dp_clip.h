//
// Created for Falcon VFL-MLP differential-privacy clipping protocol.
//
// Implements Π_DPClip:
//   (1) Π_C2S  : convert ciphertext matrix to secret shares
//   (2) SPDZ   : per-sample L2 clipping in MPC (DP_CLIP_L2 comp type)
//   (3) Local  : each party adds Gaussian noise N(0, (σ_dp/√m)² I) to
//                its own share; by Gaussian additivity the aggregate noise
//                is N(0, σ_dp² I)
//   (4) Π_S2C  : convert clipped+noised shares back to ciphertext matrix
//
// Pure-math helpers (compute_clip_factors, clip_shares_mat_inplace,
// add_gaussian_noise_to_shares) can be tested without Party/SPDZ.
//

#ifndef FALCON_INCLUDE_FALCON_ALGORITHM_VERTICAL_NN_DP_CLIP_H_
#define FALCON_INCLUDE_FALCON_ALGORITHM_VERTICAL_NN_DP_CLIP_H_

#include <falcon/operator/phe/fixed_point_encoder.h>
#include <falcon/party/party.h>

#include <vector>

/**
 * Compute per-sample L2 clipping factors for a 2-D share matrix.
 *
 * For each sample i:
 *   factor_i = min(1,  C / ||shares_mat[i]||_2)
 *
 * @param shares_mat      2-D matrix, shape (batch_size, n_outputs)
 * @param clip_threshold  L2 clipping threshold C
 * @return                vector of per-sample factors, length batch_size
 */
std::vector<double>
compute_clip_factors(const std::vector<std::vector<double>> &shares_mat,
                     double clip_threshold);

/**
 * Apply per-sample L2 clipping to a 2-D share matrix **in-place**.
 *
 * After the call, for every row i:
 *   ||shares_mat[i]||_2 <= clip_threshold
 *
 * @param shares_mat      2-D matrix, shape (batch_size, n_outputs)
 * @param clip_threshold  L2 clipping threshold C
 */
void clip_shares_mat_inplace(std::vector<std::vector<double>> &shares_mat,
                             double clip_threshold);

/**
 * Add i.i.d. Gaussian noise to each element of a 2-D share matrix **in-place**.
 *
 * Each element is perturbed with a sample from N(0, noise_sigma_per_party²).
 * When the same sigma is used on every party j (sigma_j = σ_dp / √m),
 * the sum of all parties' noise equals N(0, σ_dp² I) by Gaussian additivity.
 *
 * @param shares_mat             2-D matrix, shape (batch_size, n_outputs)
 * @param noise_sigma_per_party  per-party noise std-dev σ_dp / √m
 *                               Pass 0.0 to skip noise injection.
 */
void add_gaussian_noise_to_shares(std::vector<std::vector<double>> &shares_mat,
                                  double noise_sigma_per_party);

/**
 * Full Π_DPClip protocol applied to the layer-0 aggregated ciphertext matrix.
 *
 * This function is called **after** comp_1st_layer_agg_output produces the
 * aggregated [Ẑ_B^(0)] = Σ_k [Z_B^(0,k)] + [b^(0)], and before the
 * standard C2S → SPDZ-Activation step.
 *
 * Protocol steps (all m parties participate):
 *   1. Π_C2S  : cipher_mat → secret shares
 *   2. SPDZ   : per-sample L2 norm clipping (DP_CLIP_L2 comp type)
 *               SPDZ returns clipped shares of the same dimension
 *   3. Local  : each party adds N(0, (σ_dp/√m)² I) to its own shares
 *   4. Π_S2C  : clipped+noised shares → result_mat ciphertexts
 *
 * @param party           initialized party object
 * @param cipher_mat      input  ciphertext matrix, shape (batch_size, n_outputs)
 * @param result_mat      output ciphertext matrix (pre-allocated),
 *                        shape (batch_size, n_outputs)
 * @param batch_size      number of samples B
 * @param n_outputs       number of layer-0 output neurons n_1
 * @param clip_threshold  per-sample L2 clipping threshold C
 * @param noise_sigma_dp  total Gaussian noise std-dev σ_dp (split among parties)
 */
void dp_clip_layer0_outputs(const Party &party, EncodedNumber **cipher_mat,
                            EncodedNumber **result_mat, int batch_size,
                            int n_outputs, double clip_threshold,
                            double noise_sigma_dp);

#endif // FALCON_INCLUDE_FALCON_ALGORITHM_VERTICAL_NN_DP_CLIP_H_
