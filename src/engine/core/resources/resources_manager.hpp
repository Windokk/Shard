#pragma once

#include <unordered_map>

#include "engine/filesystem/filesystem.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/levels/level.hpp"

namespace Shard::Engine::Rendering{
    class Renderer;
    class Mesh;
    class Texture2D;
    struct ProbeBakeData;
    class Shader;
    class ComputeShader;
    class Material;
}

namespace Shard::Engine::Audio{
    class SoundAsset;
}

namespace Shard::Engine::Core::Resources{

    class ResourcesManager{
        public:
            
            /// @brief Indexes all assets in the project and engine directories with unique IDs.
            /// Scans `projectDir` and `engineDir`, assigning each asset a unique ID via the Asset ID Manager.
            /// This enables consistent asset lookup and loading at runtime.
            /// @param projectResDir Path to the project's asset directory.
            void ConstructGlobalFileIndex(const Filesystem::Path &projectResDir);
            
            /// @brief Loads a FBX model from a path and adds it to the project's loaded models lists
            /// @param name The name of the model in the project
            /// @param path The normalized path at which the model is located (filename + extension expected)
            /// @return A shared pointer to a Mesh
            std::shared_ptr<Rendering::Mesh> LoadModel(const std::string &pathInProject, const Filesystem::Path &path);
            
            /// @brief Loads a texture2D from a given path and adds it to the project's loaded textures list
            /// @param name The name of the texture2D in the project
            /// @param path The normalized path at which the texture2D is located (filename + extension expected)
            /// @return A shared pointer to a Texture2D
            std::shared_ptr<Rendering::Texture2D> LoadTexture(const std::string &pathInProject, const Filesystem::Path &path);
            
            std::shared_ptr<Rendering::EnvironmentMap> LoadEnvMap(const std::string &pathInProject, const Filesystem::Path &path);

            /// @brief Loads a shader program from given vertex, fragment, and geometry shader paths and adds it to the project's loaded shaders list
            /// @param name The name of the shader program in the project
            /// @param vsPath The normalized path to the vertex shader source file
            /// @param fsPath The normalized path to the fragment shader source file
            /// @param gsPath The normalized path to the geometry shader source file (optional or empty if not used)
            /// @return A shared pointer to a Shader
            std::shared_ptr<Rendering::Shader> LoadShader(const std::string &pathInProject, const Filesystem::Path &vsPath, const Filesystem::Path &fsPath, const Filesystem::Path &gsPath);

            /// @brief Loads a compute shader from a given single-file source path and adds it to the project's loaded compute shaders list
            /// @param name The name of the compute shader in the project
            /// @param path The normalized path to the compute shader source file (.comp)
            /// @return A shared pointer to a ComputeShader
            std::shared_ptr<Rendering::ComputeShader> LoadComputeShader(const std::string &pathInProject, const Filesystem::Path &path);

            /// @brief Loads a material from a given path and adds it to the project's loaded materials list
            /// @param name The name of the material in the project
            /// @param path The normalized path at which the material is located (filename + extension expected)
            /// @return A shared pointer to a Material
            std::shared_ptr<Rendering::Material> LoadMaterial(const std::string &pathInProject, const Filesystem::Path &path);

            /// @brief Loads a level from a given path and adds it to the project's loaded levels list
            /// @param name The name of the level in the project
            /// @param path The normalized path at which the level is located (filename + extension expected)
            /// @return A shared pointer to a Level
            std::shared_ptr<Levels::Level> LoadLevel(const std::string &pathInProject, const Filesystem::Path& path);

            /// @brief Loads the raw bytes of a sound file and adds it to the project's loaded sounds list
            /// @param name The name of the sound in the project
            /// @param path The normalized path at which the sound is located (filename + extension expected)
            /// @return A shared pointer to a SoundAsset
            std::shared_ptr<Audio::SoundAsset> LoadSound(const std::string &pathInProject, const Filesystem::Path &path);

            /// @brief Retrieves mesh from the project's loaded meshes lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the mesh in the project
            /// @return A shared pointer to a Mesh
            std::shared_ptr<Rendering::Mesh> GetMesh(std::string pathInProject);

            /// @brief Retrieves material from the project's loaded materials lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the material in the project
            /// @return A shared pointer to a Material
            std::shared_ptr<Rendering::Material> GetMaterial(std::string pathInProject);

            /// @brief Retrieves shader from the project's loaded shaders lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the shader in the project
            /// @return A shared pointer to a Shader
            std::shared_ptr<Rendering::Shader> GetShader(std::string pathInProject);

            /// @brief Retrieves compute shader from the project's loaded compute shaders lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the compute shader in the project
            /// @return A shared pointer to a ComputeShader
            std::shared_ptr<Rendering::ComputeShader> GetComputeShader(std::string pathInProject);

            /// @brief Retrieves a texture2D from the project's loaded textures lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the texture2D in the project
            /// @return A shared pointer to a Texture
            std::shared_ptr<Rendering::Texture2D> GetTexture(std::string pathInProject);

            std::shared_ptr<Rendering::EnvironmentMap> GetEnvMap(std::string pathInProject);

            /// @brief Retrieves level from the project's loaded levels lists (Tries to load it if it isn't loaded yet)
            /// @param name The name of the level in the project
            /// @return A shared pointer to a Level
            std::shared_ptr<Levels::Level> GetLevel(const std::string& pathInProject);

            /// @brief Retrieves a sound's raw bytes from the project's loaded sounds list (Tries to load it if it isn't loaded yet)
            /// @param name The name of the sound in the project
            /// @return A shared pointer to a SoundAsset
            std::shared_ptr<Audio::SoundAsset> GetSound(std::string pathInProject);

            /// @brief Retrieves a baked probe volume's CPU-side data (Tries to load it if it isn't loaded yet - normally the level prefetcher already decoded it on a worker thread, see AdoptProbeBake)
            /// @param pathInProject The path of the .probes file in the project
            /// @return The decoded data, or null if the file is unknown or unreadable
            std::shared_ptr<Rendering::ProbeBakeData> GetProbeBake(const std::string& pathInProject);

            /// @brief Inserts an already-built mesh/texture into the cache under `pathInProject`. No-op if `pathInProject` is already cached.
            void AdoptMesh(const std::string &pathInProject, std::shared_ptr<Rendering::Mesh> mesh);
            void AdoptTexture(const std::string &pathInProject, std::shared_ptr<Rendering::Texture2D> texture);
            void AdoptProbeBake(const std::string &pathInProject, std::shared_ptr<Rendering::ProbeBakeData> probeBake);

            /// @brief True if `pathInProject` is already resident in the mesh/texture/probe bake cache.
            bool HasMesh(const std::string &pathInProject) const;
            bool HasTexture(const std::string &pathInProject) const;
            bool HasProbeBake(const std::string &pathInProject) const;

            /// @brief Unloads all unused dependencies of a given asset (recursively)
            /// @param assetName The name of the "root" asset
            void UnLoadDependencies(const std::string &assetName);

            void UnloadMesh(const std::string& name);
            void UnloadMaterial(const std::string& name);
            void UnloadShader(const std::string& name);
            void UnloadComputeShader(const std::string& name);
            void UnloadImage(const std::string& name);
            void UnloadEnvMap(const std::string& name);
            void UnloadLevel(const std::string& name);
            void UnloadSound(const std::string& name);

            /// @brief Drops the CPU-side copy of a probe bake. Baked data is only needed until it has been uploaded to the GPU (or, after a re-bake, until the stale copy has to make way for the new file), so unlike textures it is not kept resident.
            void UnloadProbeBake(const std::string& name);

        private:

            std::unordered_map<std::string, std::shared_ptr<Rendering::Mesh>> meshes;
            std::unordered_map<std::string, std::shared_ptr<Rendering::Texture2D>> textures;
            std::unordered_map<std::string, std::shared_ptr<Rendering::EnvironmentMap>> envmaps;
            std::unordered_map<std::string, std::shared_ptr<Rendering::Shader>> shaders;
            std::unordered_map<std::string, std::shared_ptr<Rendering::ComputeShader>> computeShaders;
            std::unordered_map<std::string, std::shared_ptr<Rendering::Material>> materials;
            std::unordered_map<std::string, std::shared_ptr<Levels::Level>> levels;
            std::unordered_map<std::string, std::shared_ptr<Audio::SoundAsset>> sounds;
            std::unordered_map<std::string, std::shared_ptr<Rendering::ProbeBakeData>> probeBakes;
        };

}