// Created by RED on 03.12.2025.

#include "utils/smd_parser.h"

#include <cctype>
#include <charconv>
#if defined(__APPLE__)
#include <Availability.h>
#if __MAC_OS_X_VERSION_MIN_REQUIRED < 260000
// libc++ only provides floating point std::from_chars on macOS 26+
#define PYLIB_STRTOF_FALLBACK 1
#include <string>
#include <cstdlib>
#include <xlocale.h>
#endif
#endif

using namespace smd;

Parser::Parser(std::string_view source)
    : m_src(source), m_pos(0) {
}

std::string_view Parser::trim(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

bool Parser::nextLogicalLine(std::string_view &out) {
    while (m_pos < m_src.size()) {
        std::size_t end = m_src.find('\n', m_pos);
        if (end == std::string_view::npos) {
            end = m_src.size();
        }
        std::string_view line = m_src.substr(m_pos, end - m_pos);
        m_pos = end + (end < m_src.size() ? 1 : 0);

        // strip trailing '\r'
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        // strip comments: everything after //
        std::size_t cpos = line.find("//");
        if (cpos != std::string_view::npos) {
            line = line.substr(0, cpos);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        out = line;
        return true;
    }
    return false;
}

std::vector<std::string_view> Parser::tokenize(std::string_view s) {
    std::vector<std::string_view> out;
    s = trim(s);
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i >= s.size()) break;
        std::size_t j = i;
        while (j < s.size() &&
               !std::isspace(static_cast<unsigned char>(s[j]))) {
            ++j;
        }
        out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}


// stoi/stof accepted one leading '+'; from_chars does not.
static std::string_view strip_plus(std::string_view sv) {
    if (sv.size() > 1 && sv[0] == '+' && sv[1] != '-' && sv[1] != '+') sv.remove_prefix(1);
    return sv;
}

int Parser::toInt(std::string_view sv) {
    if (m_error) return 0;
    const std::string_view s = strip_plus(sv);
    int value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || ec != std::errc() || ptr != s.data() + s.size()) {
        setError("expected integer, got: '" + std::string(sv) + "'");
        return 0;
    }
    return value;
}

float Parser::toFloat(std::string_view sv) {
    if (m_error) return 0.f;
    const std::string_view s = strip_plus(sv);
    float value = 0.f;
    // Locale-independent, unlike stof.
#ifdef PYLIB_STRTOF_FALLBACK
    const std::string text(s);
    char *end = nullptr;
    value = strtof_l(text.c_str(), &end, LC_C_LOCALE);
    // strtof also takes leading whitespace and hex; from_chars (general format) takes neither
    const bool ok = !text.empty() && !std::isspace(static_cast<unsigned char>(text[0])) &&
                    text.find_first_of("xX") == std::string::npos && end == text.c_str() + text.size();
    if (!ok) {
#else
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || ec != std::errc() || ptr != s.data() + s.size()) {
#endif
        setError("expected float, got: '" + std::string(sv) + "'");
        return 0.f;
    }
    return value;
}

void Parser::setError(const std::string &msg) {
    if (!m_error) {
        m_error = true;
        m_errorMsg = "SMD parse error: " + msg;
    }
}

bool Parser::parse(SMDData *out) {
    parseTopLevel(*out);
    deduceKind(*out);
    return !m_error;
}

void Parser::parseTopLevel(SMDData &out) {
    std::string_view line;
    bool versionSeen = false;
    while (!m_error && nextLogicalLine(line)) {
        auto tok = tokenize(line);
        if (tok.empty()) continue;

        if (tok[0] == "version") {
            if (versionSeen) {
                setError("duplicate version header");
            }
            parseVersion(line, out);
            versionSeen = true;
        } else if (tok[0] == "nodes") {
            parseNodes(out);
        } else if (tok[0] == "skeleton") {
            parseSkeleton(out);
        } else if (tok[0] == "triangles") {
            parseTriangles(out);
        } else if (tok[0] == "vertexanimation") {
            parseVertexAnimation(out);
        } else if (tok[0] == "end") {
            // top-level 'end' – ignore
            continue;
        } else {
            // Some exporters might put things like 'nodes' preceded by comments,
            // but anything else at top-level is suspicious.
            // We'll be lenient and ignore unknown top-level tokens.
            continue;
        }
    }
    if (!m_error && !versionSeen) {
        setError("missing 'version' line");
    }
}

void Parser::parseVersion(std::string_view line, SMDData &out) {
    auto tok = tokenize(line);
    if (tok.size() < 2) {
        setError("version line must be: 'version <int>'");
        return;
    }
    out.version = toInt(tok[1]);
}

void Parser::parseNodes(SMDData &out) {
    if (m_error) return;

    std::string_view line;
    while (!m_error && nextLogicalLine(line)) {
        if (line == "end") break;

        // Parse...
        auto q1 = line.find('"');
        if (q1 == std::string_view::npos) {
            setError("node line missing opening quote");
            return;
        }

        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string_view::npos) {
            setError("node line missing closing quote");
            return;
        }

        std::string_view before = trim(line.substr(0, q1));
        if (before.empty()) {
            setError("node missing id");
            return;
        }

        Node n;
        n.id = toInt(before);
        n.name = std::string(line.substr(q1 + 1, q2 - q1 - 1));

        std::string_view after = trim(line.substr(q2 + 1));
        if (after.empty()) {
            setError("node missing parent index");
            return;
        }
        n.parent = toInt(after);

        out.nodes.push_back(std::move(n));
    }
}

void Parser::parseSkeleton(SMDData &out) {
    std::string_view line;
    int currentTime = 0;
    bool haveTime = false;

    while (!m_error && nextLogicalLine(line)) {
        if (line == "end") {
            break;
        }

        auto tok = tokenize(line);
        if (tok.empty()) continue;

        if (tok[0] == "time") {
            if (tok.size() < 2) {
                setError("skeleton 'time' line must be: 'time <int>'");
                return;
            }
            currentTime = toInt(tok[1]);
            haveTime = true;
            // Ensure frame exists
            out.skeleton.frames[currentTime];
        } else {
            if (!haveTime) {
                setError("bone transform line before any 'time' in skeleton");
                return;
            }
            if (tok.size() < 7) {
                setError("skeleton bone line must have 7 fields");
                return;
            }
            BoneDef k{};
            k.bone = toInt(tok[0]);
            k.pos[0] = toFloat(tok[1]);
            k.pos[1] = toFloat(tok[2]);
            k.pos[2] = toFloat(tok[3]);
            k.rot[0] = toFloat(tok[4]);
            k.rot[1] = toFloat(tok[5]);
            k.rot[2] = toFloat(tok[6]);
            out.skeleton.frames[currentTime].push_back(k);
        }
    }
}

void Parser::parseTriangles(SMDData &out) {
    std::string_view line;
    while (!m_error) {
        if (!nextLogicalLine(line)) break;
        if (line == "end") {
            break;
        }

        // First non-'end' line in this position is a material name
        Triangle tri;
        tri.material = std::string(line);

        for (int i = 0; i < 3; ++i) {
            if (!nextLogicalLine(line)) {
                setError("unexpected EOF while reading triangle vertices");
                return;
            }
            auto tok = tokenize(line);
            if (tok.size() < 9) {
                setError("triangle vertex line must have at least 9 fields");
                return;
            }

            Vertex v{};
            int32_t firstBone = toInt(tok[0]);
            v.pos[0] = toFloat(tok[1]);
            v.pos[1] = toFloat(tok[2]);
            v.pos[2] = toFloat(tok[3]);
            v.normal[0] = toFloat(tok[4]);
            v.normal[1] = toFloat(tok[5]);
            v.normal[2] = toFloat(tok[6]);
            v.uv[0] = toFloat(tok[7]);
            v.uv[1] = toFloat(tok[8]);

            // Optional weights:
            // parent x y z nx ny nz u v [nWeights bone weight ...]
            if (tok.size() > 9) {
                int count = toInt(tok[9]);
                if (m_error) return;
                // `count` comes from the file: bound it by the tokens actually
                // present before using it for indexing or allocation.
                if (count < 0 || static_cast<std::size_t>(count) > (tok.size() - 10) / 2) {
                    setError("triangle vertex weights truncated or invalid count");
                    return;
                }
                v.weights.reserve(static_cast<std::size_t>(count) + 1);

                float remainder = 1.0f;
                for (int w = 0; w < count; ++w) {
                    VertexWeight vw{};
                    vw.bone = toInt(tok[10 + w * 2]);
                    vw.weight = toFloat(tok[11 + w * 2]);
                    v.weights.push_back(vw);
                    remainder -= vw.weight;
                }
                if (remainder > 1e-5f) {
                    v.weights.emplace(v.weights.begin(), firstBone, 0.f); // first bone with implicit weight
                    v.weights[0].weight = remainder;
                }
            }

            tri.v[i] = std::move(v);
        }

        out.triangles.push_back(std::move(tri));
    }
}

void Parser::parseVertexAnimation(SMDData &out) {
    std::string_view line;
    int currentTime = 0;
    bool haveTime = false;

    while (!m_error && nextLogicalLine(line)) {
        if (line == "end") {
            break;
        }

        auto tok = tokenize(line);
        if (tok.empty()) continue;

        if (tok[0] == "time") {
            if (tok.size() < 2) {
                setError("vertexanimation 'time' line must be: 'time <int>'");
                return;
            }
            currentTime = toInt(tok[1]);
            haveTime = true;
            out.vertexAnimation.frames[currentTime];
        } else {
            if (!haveTime) {
                setError("vertexanimation vertex line before any 'time'");
                return;
            }
            if (tok.size() < 7) {
                setError("vertexanimation vertex line must have 7 fields");
                return;
            }

            VAKeyVertex v{};
            v.refIndex = toInt(tok[0]);
            v.pos[0] = toFloat(tok[1]);
            v.pos[1] = toFloat(tok[2]);
            v.pos[2] = toFloat(tok[3]);
            v.normal[0] = toFloat(tok[4]);
            v.normal[1] = toFloat(tok[5]);
            v.normal[2] = toFloat(tok[6]);

            out.vertexAnimation.frames[currentTime].push_back(v);
        }
    }
}

void Parser::deduceKind(SMDData &out) {
    bool hasVA = !out.vertexAnimation.frames.empty();
    bool hasTris = !out.triangles.empty();
    std::size_t frameCount = out.skeleton.frames.size();

    if (hasVA) {
        out.kind = FileKind::VertexAnimation;
    } else if (hasTris && frameCount == 1) {
        out.kind = FileKind::ReferenceMesh;
    } else if (!hasTris && frameCount >= 1) {
        out.kind = FileKind::Animation;
    } else {
        out.kind = FileKind::Unknown;
    }
}
