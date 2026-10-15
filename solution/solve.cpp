// max(0, x) for every element, over CKKS, on Microsoft SEAL.
//
// The circuit is the ReLUFunction component from fairmath/polycircuit
// (Apache-2.0) — the winning approach of the FHERMA ReLU challenge's
// depth-constrained track: a MILP-optimized 16th-order polynomial whose
// leading coefficient is the integer -54, folded in by repeated subtraction
// so the whole evaluation fits in four multiplicative levels. The
// coefficients, the monomial tree and the order of operations are the
// component's; the OpenFHE row here runs the same ones.
//
// What this file has that the component does not is the arithmetic of
// levels. OpenFHE under an automatic scaling technique aligns the operands
// of every multiplication and every addition itself: the tree mixes terms of
// depth one with terms of depth four and the library sorts it out. SEAL
// does none of that — it compares scales exactly and refuses operands at
// different parameters — so each product rescales and takes the nominal
// scale back, and each addend is switched down to the level of the deepest
// before the sum is taken. That is the measurement: the same polynomial, and
// the bookkeeping a library without an automatic technique leaves to the
// author.
#include <vector>

#include "solve.h"

namespace {

/** The chain index: it counts down from the top, so deeper is smaller. */
std::size_t depth_of(const Public& fhe, const Ciphertext& what) {
    return fhe.context.get_context_data(what.parms_id())->chain_index();
}

/** Down to the level of `to`, so a sum or a product is admitted. */
void align(const Public& fhe, Ciphertext& what, const Ciphertext& to) {
    if (what.parms_id() != to.parms_id())
        fhe.evaluator.mod_switch_to_inplace(what, to.parms_id());
}

/** A product of two ciphertexts: relinearise, rescale, nominal scale back. */
Ciphertext times(const Public& fhe, const Ciphertext& left, const Ciphertext& right) {
    Ciphertext a = left;
    Ciphertext b = right;
    if (depth_of(fhe, a) > depth_of(fhe, b)) align(fhe, a, b);
    else if (depth_of(fhe, b) > depth_of(fhe, a)) align(fhe, b, a);

    Ciphertext out;
    fhe.evaluator.multiply(a, b, out);
    fhe.evaluator.relinearize_inplace(out, fhe.relin_keys);
    fhe.evaluator.rescale_to_next_inplace(out);
    out.scale() = fhe.scale;
    return out;
}

/** A product by a real coefficient, and the level it costs. */
Ciphertext scaled(const Public& fhe, const Ciphertext& what, double by) {
    Plaintext factor;
    fhe.ckks->encode(by, what.parms_id(), fhe.scale, factor);
    Ciphertext out;
    fhe.evaluator.multiply_plain(what, factor, out);
    fhe.evaluator.rescale_to_next_inplace(out);
    out.scale() = fhe.scale;
    return out;
}

}  // namespace

void* solve_init(const fherma::Point& p, const Public& fhe) {
    return nullptr;
}

std::vector<Plaintext> solve_encoding(const Public& fhe,
                                      const fherma::Inputs& inp) {
    // The whole vector in one packing, slot i holding element i.
    std::vector<double> xs(inp.xs.data.begin(), inp.xs.data.end());
    std::vector<Plaintext> packings(1);
    fhe.ckks->encode(xs, fhe.scale, packings[0]);
    return packings;
}

std::vector<Ciphertext> solve_run(
    void* state,
    const Public& fhe,
    const std::vector<Ciphertext>& cts) {
    // Polynomial of order 16 with leading integer coefficient.
    static const std::vector<double> ai{
        0.0323949878919212, 0.500001412106499, 2.13483086933591, -4.78160418051218e-05,
        -13.9205486530553, 0.00061641818435605, 70.0957556465309, -0.00388040016141976,
        -213.087053403128, 0.0129145434432087, 385.924971250905, -0.0230082806472531,
        -407.029727261512, 0.0206280915579812, 230.348436664049, -0.00728306945833855};
    const int b = -54;

    // The monomial tree: eight powers, each one product deep in the one
    // before it, as the component builds them.
    const Ciphertext& x1 = cts[0];
    const Ciphertext x2 = times(fhe, x1, x1);
    const Ciphertext x3 = times(fhe, x1, x2);
    const Ciphertext x4 = times(fhe, x2, x2);
    const Ciphertext x5 = times(fhe, x1, x4);
    const Ciphertext x6 = times(fhe, x4, x2);
    const Ciphertext x7 = times(fhe, x3, x4);
    const Ciphertext x8 = times(fhe, x4, x4);

    // The terms. The first eight are a coefficient on a power; the rest
    // reach their degree through x8, which is what keeps the depth at four.
    std::vector<Ciphertext> terms;
    terms.reserve(16);
    terms.push_back(scaled(fhe, x1, ai[1]));
    terms.push_back(scaled(fhe, x2, ai[2]));
    terms.push_back(scaled(fhe, x3, ai[3]));
    terms.push_back(scaled(fhe, x4, ai[4]));
    terms.push_back(scaled(fhe, x5, ai[5]));
    terms.push_back(scaled(fhe, x6, ai[6]));
    terms.push_back(scaled(fhe, x7, ai[7]));
    terms.push_back(scaled(fhe, x8, ai[8]));

    terms.push_back(times(fhe, scaled(fhe, x1, ai[9]), x8));
    terms.push_back(times(fhe, scaled(fhe, x2, ai[10]), x8));
    terms.push_back(times(fhe, scaled(fhe, x3, ai[11]), x8));
    terms.push_back(times(fhe, scaled(fhe, x4, ai[12]), x8));
    terms.push_back(times(fhe, times(fhe, scaled(fhe, x1, ai[13]), x4), x8));
    terms.push_back(times(fhe, times(fhe, scaled(fhe, x2, ai[14]), x4), x8));
    terms.push_back(
        times(fhe, times(fhe, times(fhe, scaled(fhe, x1, ai[15]), x2), x4), x8));

    // The sum, at the level of the deepest term: a shallower addend is
    // switched down to meet it, which is the one thing SEAL will not do on
    // its own.
    std::size_t deepest = 0;
    for (std::size_t i = 1; i < terms.size(); i++)
        if (depth_of(fhe, terms[i]) < depth_of(fhe, terms[deepest])) deepest = i;

    Ciphertext total = terms[deepest];
    for (std::size_t i = 0; i < terms.size(); i++) {
        if (i == deepest) continue;
        align(fhe, terms[i], total);
        fhe.evaluator.add_inplace(total, terms[i]);
    }

    // The constant term, as a plaintext at the level the sum reached.
    Plaintext constant;
    fhe.ckks->encode(ai[0], total.parms_id(), total.scale(), constant);
    fhe.evaluator.add_plain_inplace(total, constant);

    // The leading coefficient, by subtraction: x^16 is already as deep as
    // the chain goes, so a multiplication by -54 has no level left to spend
    // and fifty-four subtractions cost none.
    Ciphertext x16 = times(fhe, x8, x8);
    align(fhe, x16, total);
    for (int m = 0; m < -b; m++) fhe.evaluator.sub_inplace(total, x16);

    return {total};
}

fherma::Outputs solve_decoding(const fherma::Point& p,
                               const Public& fhe,
                               const std::vector<Plaintext>& pts) {
    std::vector<double> values;
    fhe.ckks->decode(pts.at(0), values);

    fherma::Outputs out{};
    out.r.shape = {static_cast<int64_t>(p.N)};
    out.r.data.assign(values.begin(), values.begin() + p.N);
    return out;
}

void solve_free(void* state) {}
