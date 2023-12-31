#pragma once
#include "vkcommon.hh"
#include "vkbuffer.hh"
#include "renderer/render_types.hh"

#include <memory>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

// Structure for a model
class VKModel {
    public:
        // // Structure for a vertex in the model
        // struct Vertex {
        //     glm::vec3 position{};
        //     glm::vec3 color{};
        //     // TODO: add normals and uv's

        //     static std::vector<VkVertexInputBindingDescription> GetBindingDesc();
        //     static std::vector<VkVertexInputAttributeDescription> GetAttribDesc();
        //     bool operator==(const Vertex& other) const {
        //         return position == other.position
        //                && color == other.color;
        //     }
        // };

        // // Used to create data for the model
        // struct Builder {
        //     std::vector <Vertex> vertices{};
        //     std::vector <uint32_t> indices{};

        //     // void LoadModels(const std::string& filepath);
        // };

        // Constructors and Operators
        VKModel(VKCommonParameters &params, const Builder& builder) 
            : m_vkparams(params) {
            _create_vertex_buffers(builder.vertices);
            _create_index_buffers(builder.indices);
        }
        ~VKModel() {
        }
        VKModel(const VKModel&) = delete;
        VKModel &operator=(const VKModel&) = delete;

        
        // static std::unique_ptr<VKModel> CreateModelFromFile();
        void Bind(VkCommandBuffer cmdBuffer);
        void Draw(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
        void Destroy();


    private:
        void _create_vertex_buffers(const std::vector <Vertex> &vertices);
        void _create_index_buffers(const std::vector <uint32_t> &indices);

        std::unique_ptr<VKBuffer> m_vbuffer;
        std::unique_ptr<VKBuffer> m_ibuffer;

        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;
        bool m_hasIndexBuffer = false;
        VKCommonParameters &m_vkparams; // has lifetime of renderer -- outlives the model
};
