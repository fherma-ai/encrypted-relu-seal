// GENERATED for relu/relu@1.0.0. Do not edit — `--update` rewrites it.
//
//     ./solution <point directory>
//
// Reads the directory a bundle prepared, answers every case in it, and writes
// what each one cost.
//
//     manifest.json     the point, and how many cases      read
//     cases/000000/     one file per argument              read
//     out/000000/       one file per result                written
//     out/results.json  what each case cost                written
//
// Only the call to solve_run is timed. Reading and writing are outside the
// window, which is also why seeing the inputs early is not a way to answer
// early: nothing of yours runs during the read.
#include "envelope.h"
#include "fherma.h"
#include "solve.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& at) {
    std::ifstream file(at, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + at.string());
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

// A scanner for our own manifest and nothing else. The schema is two things —
// the point and a count — so this beats a dependency that would have to be
// fetched at build time inside a sandbox with no network.
double number(const std::string& text, const std::string& key) {
    const std::string quoted = "\"" + key + "\"";
    const size_t at = text.find(quoted);
    if (at == std::string::npos) throw std::runtime_error("no " + key + " in manifest.json");
    return std::strtod(text.c_str() + text.find(':', at + quoted.size()) + 1, nullptr);
}

std::string numbered(size_t i) {
    std::ostringstream out;
    out << std::setw(6) << std::setfill('0') << i;
    return out.str();
}

template <class T>
fherma::Tensor<T> read(const fs::path& where, const std::string& name,
                       std::vector<int64_t> shape) {
    fherma::Tensor<T> tensor;
    tensor.shape = std::move(shape);

    const std::string raw = slurp(where / (name + ".bin"));
    const size_t count = static_cast<size_t>(tensor.count());
    if (raw.size() != count * sizeof(T)) {
        throw std::runtime_error(name + ": " + std::to_string(raw.size()) +
                                 " bytes for " + std::to_string(count) + " values");
    }

    // Little-endian on the wire, and on every platform this runs on, so the
    // copy is the decode.
    tensor.data.resize(count);
    std::memcpy(tensor.data.data(), raw.data(), raw.size());
    return tensor;
}

template <class T>
void write(const fs::path& where, const std::string& name,
           const fherma::Tensor<T>& tensor) {
    std::ofstream out(where / (name + ".bin"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(tensor.data.data()),
              static_cast<std::streamsize>(tensor.data.size() * sizeof(T)));
}

// A scalar on the wire is a tensor without dimensions: one value of its
// width, in its own file.
template <class T>
T read_one(const fs::path& where, const std::string& name) {
    const std::string raw = slurp(where / (name + ".bin"));
    if (raw.size() != sizeof(T)) {
        throw std::runtime_error(name + ": " + std::to_string(raw.size()) +
                                 " bytes for one value");
    }
    T value;
    std::memcpy(&value, raw.data(), sizeof(T));
    return value;
}

template <class T>
void write_one(const fs::path& where, const std::string& name, T value) {
    std::ofstream out(where / (name + ".bin"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Written after every case, not at the end: a process killed on its timeout
// has still done the cases before it, and a file written once at the end would
// throw them away.
void report(const fs::path& out, double envelope_s, double init_s,
            const std::vector<std::string>& cases) {
    std::ofstream file(out / "results.json");
    file << std::fixed << std::setprecision(9);
    file << "{\"envelope_s\":" << envelope_s << ",\"init_s\":" << init_s << ",\"cases\":[";
    for (size_t i = 0; i < cases.size(); ++i) {
        if (i) file << ",";
        file << cases[i];
    }
    file << "]}";
}

std::string ok(size_t i, double seconds,
               size_t cts_in, size_t cts_out, long result_level,
               double encoding_s, double encrypt_s,
               double decrypt_s, double decoding_s) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(9);
    out << "{\"i\":" << i << ",\"seconds\":" << seconds << ",\"status\":\"ok\""
        << ",\"ciphertexts_in\":" << cts_in << ",\"ciphertexts_out\":" << cts_out
        << ",\"encoding_s\":" << encoding_s << ",\"encrypt_s\":" << encrypt_s
        << ",\"decrypt_s\":" << decrypt_s << ",\"decoding_s\":" << decoding_s;
    if (result_level >= 0) out << ",\"result_level\":" << result_level;
    out << "}";
    return out.str();
}

std::string crashed(size_t i, const std::string& why) {
    std::ostringstream out;
    out << "{\"i\":" << i << ",\"seconds\":null,\"status\":\"crashed\",\"note\":\"";
    for (char c : why.substr(0, 200)) {
        if (c == '"' || c == '\\') out << '\\';
        out << (c == '\n' ? ' ' : c);
    }
    out << "\"}";
    return out.str();
}

}  // namespace

namespace fherma_envelope {
Config Config::load(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    // The config is JSONC: strip // to end of line, then read by name.
    std::string text;
    {
        std::stringstream lines(buffer.str());
        std::string line;
        while (std::getline(lines, line)) {
            text += line.substr(0, line.find("//"));
            text += '\n';
        }
    }
    auto value_at = [&](const char* name) {
        const auto at = text.find(std::string("\"") + name + "\"");
        return at == std::string::npos ? std::string::npos : text.find(':', at) + 1;
    };
    [[maybe_unused]] auto grab_u32 = [&](const char* name, uint32_t& into) {
        const auto at = value_at(name);
        if (at != std::string::npos) into = std::stoul(text.substr(at));
    };
    [[maybe_unused]] auto grab_u64 = [&](const char* name, uint64_t& into) {
        const auto at = value_at(name);
        if (at != std::string::npos) into = std::stoull(text.substr(at));
    };
    auto grab_string = [&](const char* name, std::string& into) {
        const auto at = value_at(name);
        if (at == std::string::npos) return;
        const auto open = text.find('"', at);
        const auto close = text.find('"', open + 1);
        into = text.substr(open + 1, close - open - 1);
    };
    [[maybe_unused]] auto grab_bool = [&](const char* name, bool& into) {
        const auto at = value_at(name);
        if (at != std::string::npos)
            into = text.compare(text.find_first_not_of(" \t", at), 4, "true") == 0;
    };
    [[maybe_unused]] auto ints_at = [&](const char* name, std::vector<long>& out) {
        const auto at = value_at(name);
        if (at == std::string::npos) return false;
        out.clear();
        auto cursor = text.find('[', at);
        const auto stop = text.find(']', cursor);
        while (cursor < stop) {
            const auto next = text.find_first_of(",]", cursor + 1);
            try { out.push_back(std::stol(text.substr(cursor + 1, next - cursor - 1))); }
            catch (...) {}
            cursor = next;
        }
        return true;
    };

    grab_string("scheme", cfg.scheme);
    grab_string("security", cfg.security);
    grab_u32("poly_modulus_degree", cfg.poly_modulus_degree);
    grab_u32("slot_count", cfg.slot_count);
    grab_u32("mult_depth", cfg.mult_depth);
    grab_u32("scale_mod_size", cfg.scale_mod_size);
    grab_u32("first_mod_size", cfg.first_mod_size);
    grab_u32("special_mod_size", cfg.special_mod_size);
    grab_u64("plaintext_modulus", cfg.plaintext_modulus);

    std::vector<long> ints;
    if (ints_at("coeff_modulus_bits", ints))
        cfg.coeff_modulus_bits.assign(ints.begin(), ints.end());
    if (ints_at("rotation_key_indexes", ints))
        cfg.rotation_key_indexes.assign(ints.begin(), ints.end());
    return cfg;
}
}  // namespace fherma_envelope

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: solution <point directory>" << std::endl;
        return 2;
    }

    const fs::path root = argv[1];
    const std::string manifest = slurp(root / "manifest.json");

    fherma::Point p{};
        p.N = static_cast<decltype(p.N)>(number(manifest, "N"));
    const size_t total = static_cast<size_t>(number(manifest, "cases"));

    const fs::path answers_root = root / "out";
    fs::create_directories(answers_root);

    // The config travels with the solution, not the point; the point's own
    // copy wins when a runner lays one there.
    const auto config_path = std::filesystem::exists(root / "config.jsonc")
        ? (root / "config.jsonc").string() : std::string("config.jsonc");
    const auto sealing = std::chrono::steady_clock::now();
    fherma_envelope::Config cfg = fherma_envelope::Config::load(config_path);
    fherma_envelope::Envelope envelope(cfg);
    const std::chrono::duration<double> envelope_took =
        std::chrono::steady_clock::now() - sealing;

    const auto setup = std::chrono::steady_clock::now();
    void* state = solve_init(p, envelope.pub());
    const std::chrono::duration<double> init_took =
        std::chrono::steady_clock::now() - setup;

    std::vector<std::string> cases;
    report(answers_root, envelope_took.count(), init_took.count(), cases);

    for (size_t i = 0; i < total; ++i) {
        const fs::path where = root / "cases" / numbered(i);
        const fs::path answers = answers_root / numbered(i);

        fherma::Inputs in{};
        try {
            in.xs = read<double>(where, "xs", {static_cast<int64_t>(p.N)});
        } catch (const std::exception& failure) {
            cases.push_back(crashed(i, std::string("reading the case: ") + failure.what()));
            report(answers_root, envelope_took.count(), init_took.count(), cases);
            continue;
        }

        double seconds = 0;
        size_t cts_in = 0, cts_out = 0;
        long result_level = -1;
        double encoding_s = 0, encrypt_s = 0, decrypt_s = 0, decoding_s = 0;
        fherma::Outputs answer{};
        try {
            // Every stage is timed, and only one is the score. The others are
            // reported so the whole cost of an encrypted answer is visible.
            auto mark = std::chrono::steady_clock::now();
            auto packings = solve_encoding(envelope.pub(), in);
            encoding_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mark).count();

            mark = std::chrono::steady_clock::now();
            auto cts = envelope.encrypt(packings);
            encrypt_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mark).count();

            // Monotonic, and around the call and nothing else.
            const auto started = std::chrono::steady_clock::now();
            auto out_cts = solve_run(state, envelope.pub(), cts);
            const std::chrono::duration<double> took =
                std::chrono::steady_clock::now() - started;
            seconds = took.count();

            cts_in = cts.size();
            cts_out = out_cts.size();
            if (!out_cts.empty()) {
                const auto& chain = envelope.pub().context;
                const auto top = chain.first_context_data()->chain_index();
                const auto reached = chain.get_context_data(out_cts[0].parms_id());
                if (reached)
                    result_level = static_cast<long>(top - reached->chain_index());
            }

            mark = std::chrono::steady_clock::now();
            auto pts = envelope.decrypt(out_cts);
            decrypt_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mark).count();

            mark = std::chrono::steady_clock::now();
            answer = solve_decoding(p, envelope.pub(), pts);
            decoding_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - mark).count();
        } catch (const std::exception& failure) {
            cases.push_back(crashed(i, failure.what()));
            report(answers_root, envelope_took.count(), init_took.count(), cases);
            continue;
        }

        try {
            fs::create_directories(answers);
            write(answers, "r", answer.r);
        } catch (const std::exception& failure) {
            cases.push_back(crashed(i, std::string("writing the answer: ") + failure.what()));
            report(answers_root, envelope_took.count(), init_took.count(), cases);
            continue;
        }

        cases.push_back(ok(i, seconds, cts_in, cts_out, result_level,
                           encoding_s, encrypt_s, decrypt_s, decoding_s));
        report(answers_root, envelope_took.count(), init_took.count(), cases);
    }

    solve_free(state);
    report(answers_root, envelope_took.count(), init_took.count(), cases);
    return 0;
}
