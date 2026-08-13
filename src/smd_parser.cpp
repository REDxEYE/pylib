// Created by RED on 03.12.2025.

#include "utils/smd_parser.h"

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


int Parser::toInt(std::string_view sv) {
    if (m_error) return 0;
    try {
        return std::stoi(std::string(sv));
    } catch (...) {
        setError("expected integer, got: '" + std::string(sv) + "'");
        return 0;
    }
}

float Parser::toFloat(std::string_view sv) {
    if (m_error) return 0.f;
    try {
        return std::stof(std::string(sv));
    } catch (...) {
        setError("expected float, got: '" + std::string(sv) + "'");
        return 0.f;
    }
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
    while (nextLogicalLine(line)) {
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
}

void Parser::parseVersion(std::string_view line, SMDData &out) {
    auto tok = tokenize(line);
    if (tok.size() < 2) {
        setError("version line must be: 'version <int>'");
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

    while (nextLogicalLine(line)) {
        if (line == "end") {
            break;
        }

        auto tok = tokenize(line);
        if (tok.empty()) continue;

        if (tok[0] == "time") {
            if (tok.size() < 2) {
                setError("skeleton 'time' line must be: 'time <int>'");
            }
            currentTime = toInt(tok[1]);
            haveTime = true;
            // Ensure frame exists
            out.skeleton.frames[currentTime];
        } else {
            if (!haveTime) {
                setError("bone transform line before any 'time' in skeleton");
            }
            if (tok.size() < 7) {
                setError("skeleton bone line must have 7 fields");
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
    while (true) {
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
            }
            auto tok = tokenize(line);
            if (tok.size() < 9) {
                setError("triangle vertex line must have at least 9 fields");
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
                std::size_t needed = 10 + count * 2;
                if (tok.size() < needed) {
                    setError("triangle vertex weights truncated");
                }
                v.weights.reserve(count + 1);

                float remainder = 1.0f;
                for (int w = 0; w < count; ++w) {
                    VertexWeight vw{};
                    vw.bone = toInt(tok[10 + w * 2]);
                    vw.weight = toFloat(tok[11 + w * 2]);
                    v.weights.push_back(vw);
                    remainder -= vw.weight;
                }
                if (remainder > 0) {
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

    while (nextLogicalLine(line)) {
        if (line == "end") {
            break;
        }

        auto tok = tokenize(line);
        if (tok.empty()) continue;

        if (tok[0] == "time") {
            if (tok.size() < 2) {
                setError("vertexanimation 'time' line must be: 'time <int>'");
            }
            currentTime = toInt(tok[1]);
            haveTime = true;
            out.vertexAnimation.frames[currentTime];
        } else {
            if (!haveTime) {
                setError("vertexanimation vertex line before any 'time'");
            }
            if (tok.size() < 7) {
                setError("vertexanimation vertex line must have 7 fields");
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
