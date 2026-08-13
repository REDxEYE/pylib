// Created by RED on 03.12.2025.

#ifndef PYLIB_SMD_PARSER_H
#define PYLIB_SMD_PARSER_H
#include <string>
#include <vector>
#include <map>

namespace smd {
    enum class FileKind {
        Unknown,
        ReferenceMesh, // nodes + skeleton (1 frame) + triangles
        Animation, // nodes + skeleton (2+ frames), no triangles
        VertexAnimation // vertexanimation (VTA)
    };

    struct Node {
        int32_t id;
        std::string name;
        int32_t parent;
    };

    struct BoneDef {
        int bone;
        float pos[3];
        float rot[3];
    };

    using Frame = std::vector<BoneDef>;

    struct Skeleton {
        // time -> frame
        std::map<int, Frame> frames;
    };

    struct VertexWeight {
        int bone{};
        float weight{};
    };

    struct Vertex {
        float pos[3]{};
        float normal[3]{};
        float uv[2]{};
        std::vector<VertexWeight> weights; // optional extra skinning weights
    };

    struct Triangle {
        std::string material;
        Vertex v[3];
    };

    struct VAKeyVertex {
        int refIndex{};
        float pos[3]{};
        float normal[3]{};
    };

    using VAFrame = std::vector<VAKeyVertex>;

    struct VertexAnimation {
        // time -> list of modified vertices
        std::map<int, VAFrame> frames;
    };

    struct SMDData {
        int version{1};
        std::vector<Node> nodes;
        Skeleton skeleton;
        std::vector<Triangle> triangles;
        VertexAnimation vertexAnimation;
        FileKind kind{FileKind::Unknown};
    };


    class Parser {
    public:
        explicit Parser(std::string_view source);

        bool parse(SMDData* out);

        [[nodiscard]] bool hasError() const { return m_error; }
        [[nodiscard]] const std::string& errorMessage() const { return m_errorMsg; }

    private:
        std::string_view m_src;
        std::size_t m_pos{0};
        bool m_error{false};
        std::string m_errorMsg;

        bool nextLogicalLine(std::string_view &out);
        static std::string_view trim(std::string_view s);
        static std::vector<std::string_view> tokenize(std::string_view s);

        int toInt(std::string_view s);
        float toFloat(std::string_view s);

        void setError(const std::string &msg);

        void parseTopLevel(SMDData &out);
        void parseVersion(std::string_view line, SMDData &out);
        void parseNodes(SMDData &out);
        void parseSkeleton(SMDData &out);
        void parseTriangles(SMDData &out);
        void parseVertexAnimation(SMDData &out);

        static void deduceKind(SMDData &out);
    };
}

#endif //PYLIB_SMD_PARSER_H
