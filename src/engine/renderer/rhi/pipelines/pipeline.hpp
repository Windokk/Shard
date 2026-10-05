#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <functional>

namespace Shard::Engine::Rendering{

    class Shader;
    struct RenderPass;
    enum class ShaderDataType;
    class Renderer;

    enum class PrimitiveTopology
    {
        Points,
        Lines,
        LineStrip,

        Triangles,
        TriangleStrip,
        TriangleFan
    };

    enum class CullMode
    {
        None,
        Back,
        Front
    };

    enum class PolygonMode
    {
        Fill,
        Line
    };

    enum class DepthCompareOp
    {
        Less,
        LessOrEqual,
        Greater,
        Always,
        Never
    };

    enum class BlendFactor
    {
        Zero,
        One,
        SrcAlpha,
        OneMinusSrcAlpha
    };

    enum class BlendOp
    {
        Add,
        Subtract
    };

    inline void HashCombine(std::size_t& seed, std::size_t hash)
    {
        seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct VertexElement
    {
        std::string name;
        ShaderDataType type;
        uint32_t location = 0;
        uint32_t offset = 0;
        uint32_t Size() const;

        bool operator==(const VertexElement& other) const
        {
            return type == other.type &&
                location == other.location;
        }
    };

    class VertexLayout
    {
        public:

            VertexLayout() = default;

            VertexLayout(std::initializer_list<VertexElement> elements, uint32_t stride)
                : m_Elements(elements), m_Stride(stride)
            {
            }

            const std::vector<VertexElement>& GetElements() const
            {
                return m_Elements;
            }

            uint32_t GetStride() const
            {
                return m_Stride;
            }

            bool operator==(const VertexLayout& other) const {
                return m_Elements == other.GetElements();
            }

        private:

            std::vector<VertexElement> m_Elements;
            uint32_t m_Stride = 0;
    };

    struct VertexLayoutHash
    {
        std::size_t operator()(const VertexLayout& layout) const
        {
            std::size_t hash = 0;

            const auto& elements = layout.GetElements();

            HashCombine(hash, elements.size());

            for (const auto& e : elements)
            {
                HashCombine(hash, (int)e.type);
                HashCombine(hash, e.location);
                HashCombine(hash, e.offset);
            }

            return hash;
        }
    };

    struct PipelineSpecifications
    {
        std::shared_ptr<Shader> shader;

        VertexLayout vertexLayout;

        PrimitiveTopology topology = PrimitiveTopology::Triangles;

        CullMode cullMode = CullMode::Back;
        PolygonMode polygonMode = PolygonMode::Fill;

        bool depthTest = true;
        bool depthWrite = true;
        DepthCompareOp depthCompare = DepthCompareOp::Less;

        bool blending = false;
        BlendFactor srcBlend = BlendFactor::SrcAlpha;
        BlendFactor dstBlend = BlendFactor::OneMinusSrcAlpha;
        BlendOp blendOp = BlendOp::Add;

        std::string debugName;

        bool operator==(const PipelineSpecifications& other) const;
    };

    struct PipelineSpecsHash
    {
        std::size_t operator()(const PipelineSpecifications& s) const;
    };

    class Pipeline
    {
        public:

            virtual ~Pipeline() = default;

            PipelineSpecifications& GetSpecifications()
            {
                return m_Specifications;
            };

            virtual void Invalidate() = 0;

            virtual void Bind() = 0;


        protected:
            PipelineSpecifications m_Specifications;

        private:
            static std::shared_ptr<Pipeline> Create(const PipelineSpecifications& specs);

        friend class Renderer;
    };
}