# max(0, x) for every element, elementwise — an encrypted answer

Answers `relu/relu@1.0.0` over ciphertexts, with Microsoft SEAL (CKKS).

Yours to write: **solve.cpp** — four functions. Everything else is generated
from the signature and the config, and `--update` rewrites it without
touching yours.

| | |
| --- | --- |
| secret arguments | xs — arrive in `solve_run` as ciphertexts |
| public arguments | none — arrive as plain values |
| measured | the `solve_run` call alone; encoding, encryption, decryption, decoding are outside |
| keys | generated and held by the envelope; your functions never see them |
| parameters | `config.jsonc`: prime sizes, a depth and a ring, each commented |

Your four functions take a `Public` rather than a context. SEAL splits across
`Evaluator`, `CKKSEncoder`, `Encryptor` and `Decryptor` what another library
keeps behind one handle, so the envelope gathers the public half — the
evaluator, the encoder, the evaluation keys, the scale, the slot count — and
hands it over; the secret key and the decryptor stay inside.

The security level in `config.jsonc` is held against the whole modulus, the
levels and the special prime together. Up to a ring of 32768 the computation
is SEAL's own, and the envelope asks for its verdict and raises — the library
reports an unusable context rather than throwing on one. Above 32768 SEAL's
copy of the standard's table has no row and refuses every parameter set,
however wide the ring, so the envelope carries the rows it lacks and holds
log QP against them. Either way the refusal comes before a key is generated,
and the table is the same one.
