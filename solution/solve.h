// GENERATED. The four functions that are yours, declared once.
#pragma once

#include "envelope.h"
#include "fherma.h"
#include "seal/seal.h"

using fherma_envelope::Public;
using seal::Ciphertext;
using seal::Plaintext;

// Setup over public material: the context and its keys are already made.
// Once per point, not measured.
void* solve_init(const fherma::Point& p, const Public& fhe);

// The layout: cleartext into plaintext packings, one per ciphertext.
// Reviewed together with solve_decoding as one technique.
std::vector<Plaintext> solve_encoding(const Public& fhe,
                                      const fherma::Inputs& inp);

// The answer, over ciphertexts. Measured — and only this.
std::vector<Ciphertext> solve_run(
    void* state,
    const Public& fhe,
    const std::vector<Ciphertext>& cts);

// Reading the answer out of the decrypted packings. Not measured.
fherma::Outputs solve_decoding(const fherma::Point& p,
                               const Public& fhe,
                               const std::vector<Plaintext>& pts);

void solve_free(void* state);
