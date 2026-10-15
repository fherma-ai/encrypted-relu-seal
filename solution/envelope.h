// GENERATED for relu/relu@1.0.0. Do not edit — `--update` rewrites it.
//
// The cryptographic envelope: parameters, context, keys, encryption,
// decryption. The only place a secret key exists. The measured call never
// enters this file.
//
// Every parameter comes from config.jsonc; the scheme named there decides
// which of them are read, so the config can switch schemes without
// re-scaffolding.
#pragma once

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "seal/seal.h"

namespace fherma_envelope {

struct Config {
    std::string scheme = "ckks";

    // The level SEAL holds the parameters against: tc128 | tc192 | tc256,
    // the HE standard's three classical levels. There is no entry for
    // sec_level_type::none — a context built without a floor is not a
    // measurement of this specification, and refusing the name here is
    // cheaper than discovering it on a board.
    std::string security = "tc128";

    // SEAL's name for the ring dimension. A power of two.
    uint32_t poly_modulus_degree = 16384;

    // What the ring packs, declared so a reader of the run does not have to
    // derive it. Checked against the encoder, not chosen: CKKS packs exactly
    // half the ring and the integer schemes exactly the ring. 0 declares
    // nothing and checks nothing.
    uint32_t slot_count = 0;

    // Levels: primes of `scale_mod_size` between the first modulus and the
    // special prime.
    uint32_t mult_depth = 2;

    // ckks
    uint32_t scale_mod_size = 50;
    uint32_t first_mod_size = 60;
    uint32_t special_mod_size = 60;

    // The chain written out, in bits, when the three numbers above are not
    // how the author wants to state it. Non-empty wins, and the last element
    // is the special prime.
    std::vector<int> coeff_modulus_bits;

    // bgv / bfv
    uint64_t plaintext_modulus = 65537;

    std::vector<int32_t> rotation_key_indexes;

    static Config load(const std::string& path);   // tiny JSONC reader in main
};

// The public half of the context: what an answer may use, and nothing that
// could decrypt.
//
// This is what a CryptoContext is in an OpenFHE skeleton. SEAL spreads the
// same material over several objects, so it is gathered here once and handed
// to each of the four functions.
struct Public {
    const seal::SEALContext& context;
    const seal::Evaluator& evaluator;

    // The encoder of the scheme in play; the other is null. CKKS packs reals
    // into the slots, the integer schemes pack residues modulo the plaintext
    // modulus, and nothing sensible converts between them.
    const seal::CKKSEncoder* ckks;
    const seal::BatchEncoder* integers;

    const seal::RelinKeys& relin_keys;
    const seal::GaloisKeys& galois_keys;

    // 2^scale_mod_size: what a CKKS plaintext is encoded at, and what a
    // product rescales back to. Zero for the integer schemes, which carry no
    // scale.
    double scale;

    // What the encoder actually packs.
    std::size_t slots;
};

class Envelope {
  public:
    explicit Envelope(const Config& cfg)
        : context_(parameters(cfg), true,
                   enforcing(cfg) ? level(cfg.security) : seal::sec_level_type::none) {
        // SEAL does not throw on parameters it will not accept: the context
        // is built, the flag is false, and the message says why. Asked here,
        // before a key exists.
        if (!context_.parameters_set()) {
            throw std::runtime_error(
                std::string("config.jsonc: SEAL refused these parameters — ") +
                context_.parameter_error_name() + ": " +
                context_.parameter_error_message());
        }
        // And where SEAL has no row for the ring, the floor is held here
        // instead — before a key exists either way.
        if (!enforcing(cfg)) hold_against_the_standard(cfg);

        seal::KeyGenerator keys(context_);
        secret_ = keys.secret_key();
        keys.create_public_key(public_);
        // A chain of one modulus has no key switching and no relinearization
        // key to make; asking for one throws. Such a context cannot multiply
        // either, so the answer will say so itself.
        if (context_.using_keyswitching()) keys.create_relin_keys(relin_);
        if (!cfg.rotation_key_indexes.empty()) {
            keys.create_galois_keys(
                std::vector<int>(cfg.rotation_key_indexes.begin(),
                                 cfg.rotation_key_indexes.end()),
                galois_);
        }

        encryptor_ = std::make_unique<seal::Encryptor>(context_, public_);
        decryptor_ = std::make_unique<seal::Decryptor>(context_, secret_);
        evaluator_ = std::make_unique<seal::Evaluator>(context_);
        if (cfg.scheme == "ckks") {
            ckks_ = std::make_unique<seal::CKKSEncoder>(context_);
        } else {
            integers_ = std::make_unique<seal::BatchEncoder>(context_);
        }

        const std::size_t slots = ckks_ ? ckks_->slot_count() : integers_->slot_count();
        if (cfg.slot_count && cfg.slot_count != slots) {
            throw std::runtime_error(
                "config.jsonc: slot_count declares " + std::to_string(cfg.slot_count) +
                " and this ring packs " + std::to_string(slots));
        }

        public_view_ = std::make_unique<Public>(Public{
            context_,
            *evaluator_,
            ckks_.get(),
            integers_.get(),
            relin_,
            galois_,
            cfg.scheme == "ckks" ? std::pow(2.0, static_cast<double>(cfg.scale_mod_size)) : 0.0,
            slots,
        });
    }

    const Public& pub() const { return *public_view_; }

    std::vector<seal::Ciphertext> encrypt(const std::vector<seal::Plaintext>& packings) {
        std::vector<seal::Ciphertext> out(packings.size());
        for (std::size_t i = 0; i < packings.size(); ++i)
            encryptor_->encrypt(packings[i], out[i]);
        return out;
    }

    std::vector<seal::Plaintext> decrypt(const std::vector<seal::Ciphertext>& cts) {
        std::vector<seal::Plaintext> out(cts.size());
        for (std::size_t i = 0; i < cts.size(); ++i)
            decryptor_->decrypt(cts[i], out[i]);
        return out;
    }

  private:
    static seal::EncryptionParameters parameters(const Config& cfg) {
        seal::EncryptionParameters parms(scheme_of(cfg.scheme));
        parms.set_poly_modulus_degree(cfg.poly_modulus_degree);
        parms.set_coeff_modulus(
            seal::CoeffModulus::Create(cfg.poly_modulus_degree, chain(cfg)));
        // CKKS has no plaintext modulus; setting one is an error rather than
        // a number ignored.
        if (cfg.scheme != "ckks") parms.set_plain_modulus(cfg.plaintext_modulus);
        return parms;
    }

    // The chain as bit sizes. SEAL takes the last element as the special
    // prime of the key switch, and the first as the modulus the result is
    // read at once the levels are spent, so the shape is
    //   { first, scale × mult_depth, special }
    // and the depth is the count of the middle, not of the whole list.
    static std::vector<int> chain(const Config& cfg) {
        if (!cfg.coeff_modulus_bits.empty()) return cfg.coeff_modulus_bits;
        std::vector<int> sizes;
        sizes.push_back(static_cast<int>(cfg.first_mod_size));
        for (uint32_t i = 0; i < cfg.mult_depth; ++i)
            sizes.push_back(static_cast<int>(cfg.scale_mod_size));
        sizes.push_back(static_cast<int>(cfg.special_mod_size));
        return sizes;
    }

    static seal::scheme_type scheme_of(const std::string& name) {
        static const std::map<std::string, seal::scheme_type> SCHEMES = {
            {"ckks", seal::scheme_type::ckks},
            {"bgv", seal::scheme_type::bgv},
            {"bfv", seal::scheme_type::bfv},
        };
        const auto found = SCHEMES.find(name);
        if (found == SCHEMES.end())
            throw std::runtime_error("config.jsonc: unknown scheme " + name);
        return found->second;
    }

    // Whether SEAL's own table has a row for this ring, which is when the
    // library can be left to enforce the floor itself.
    //
    // Its table stops at a ring of 32768: above that MaxBitCount returns 0,
    // the context reads that as "no modulus is secure" and refuses every
    // parameter set however wide the ring. The table is SEAL's shipping of
    // the HE standard rather than the standard itself, and the library works
    // above it — SEAL_POLY_MOD_DEGREE_MAX is 131072 — so the rows it lacks
    // are carried below and enforced here. This is the arrangement the
    // Lattigo envelope has for a library that enforces nothing at all, and
    // the numbers are the same ones.
    static bool enforcing(const Config& cfg) {
        return seal::CoeffModulus::MaxBitCount(cfg.poly_modulus_degree,
                                               level(cfg.security)) > 0;
    }

    void hold_against_the_standard(const Config& cfg) const {
        // log QP: the whole modulus the evaluation key is published under,
        // the levels and the special prime together, which is what the
        // standard's row bounds and what OpenFHE enforces over.
        const int carried =
            context_.key_context_data()->total_coeff_modulus_bit_count();
        const int allowed = standard_max_bits(cfg.poly_modulus_degree, cfg.security);
        if (carried > allowed) {
            throw std::runtime_error(
                "config.jsonc: the chain is " + std::to_string(carried) +
                " bits and a ring of " + std::to_string(cfg.poly_modulus_degree) +
                " carries at most " + std::to_string(allowed) + " at " + cfg.security);
        }
    }

    // The HE standard's rows beyond SEAL's table, ternary secret.
    static int standard_max_bits(uint32_t ring, const std::string& name) {
        static const std::map<std::string, std::map<uint32_t, int>> BEYOND = {
            {"tc128", {{65536, 1782}, {131072, 3564}}},
        };
        const auto rows = BEYOND.find(name);
        if (rows != BEYOND.end()) {
            const auto row = rows->second.find(ring);
            if (row != rows->second.end()) return row->second;
        }
        throw std::runtime_error(
            "config.jsonc: nothing bounds a ring of " + std::to_string(ring) + " at " +
            name + " — neither SEAL's own table nor the standard's rows carried here");
    }

    static seal::sec_level_type level(const std::string& name) {
        static const std::map<std::string, seal::sec_level_type> LEVELS = {
            {"tc128", seal::sec_level_type::tc128},
            {"tc192", seal::sec_level_type::tc192},
            {"tc256", seal::sec_level_type::tc256},
        };
        const auto found = LEVELS.find(name);
        if (found == LEVELS.end())
            throw std::runtime_error("config.jsonc: unknown security level " + name);
        return found->second;
    }

    seal::SEALContext context_;
    // SEAL's working objects are neither copyable nor movable, so they are
    // held by pointer and built in the constructor's body, after the context
    // they all need.
    std::unique_ptr<seal::Evaluator> evaluator_;
    std::unique_ptr<seal::Encryptor> encryptor_;
    std::unique_ptr<seal::Decryptor> decryptor_;
    std::unique_ptr<seal::CKKSEncoder> ckks_;
    std::unique_ptr<seal::BatchEncoder> integers_;
    std::unique_ptr<Public> public_view_;

    seal::SecretKey secret_;   // never handed out
    seal::PublicKey public_;
    seal::RelinKeys relin_;
    seal::GaloisKeys galois_;
};

}  // namespace fherma_envelope
