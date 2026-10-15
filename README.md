# ReLU on Microsoft SEAL

An answer to `relu/f64@1.0.0` on FHERMA with Microsoft SEAL 4.7.0. The circuit
is the ReLUFunction component of
[fairmath/polycircuit](https://github.com/fairmath/polycircuit) (Apache-2.0) —
the winning approach of the FHERMA ReLU challenge's depth-constrained track: a
MILP-optimised 16th-order polynomial whose integer leading coefficient −54 is
folded in by fifty-four subtractions, so the whole evaluation fits in four
multiplicative levels.

The coefficients and the monomial tree are the component's, as they are in the
OpenFHE row. What this answer measures beside the polynomial is what an
automatic scaling technique is worth.

The tree mixes terms one level deep with terms four deep. OpenFHE under
`FLEXIBLEAUTO` aligns the operands of every product and every sum itself; SEAL
refuses operands whose scales or parameters differ. So here each product
rescales and takes the nominal scale back, and each of the fifteen terms is
switched down to the level of the deepest before the sum is taken — the
bookkeeping is in `solve.cpp` beside the mathematics, which is the honest shape
of the comparison.

| | |
| --- | --- |
| `poly_modulus_degree` | 32768 |
| `slot_count` | 16384 — half the ring, which is the larger point exactly |
| `mult_depth` | 4 |
| `scale_mod_size` / `first_mod_size` | 48 / 60 |
| points | N = 1024, 16384 |

## Building it outside the platform

```sh
docker run --rm -v "$PWD/solution":/solution -v "$POINT":/point fherma/seal:4.7.0 \
  sh -c "cd /solution && cmake -B build -DCMAKE_BUILD_TYPE=Release \
         && cmake --build build && ./build/solution /point"
```
