#pragma once

#include "engine/core/color.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/world/objectID.hpp"

#include <string>
#include <vector>
#include <memory>

namespace Shard::Engine::Rendering{
    
    using namespace Objects::Components;

    class CameraManager{
        public:
            
            /// @brief Adds a new camera with its actor's id
            void AddCamera(const Core::ObjectID parentID, std::shared_ptr<Camera> camera) {
                cameras[parentID] = camera;

                if (!activeCamera)
                    activeCamera = camera;
            }

            /// @brief Removes a camera by name
            void RemoveCamera(const Core::ObjectID parentID) {
                if (cameras.count(parentID)) {
                    bool wasActive = cameras[parentID] == activeCamera;
                    cameras.erase(parentID);
                    if (wasActive) {
                        activeCamera = nullptr;
                        PromoteNextActiveCamera();
                    }
                }
            }

            /// @brief Picks another enabled camera to become active, e.g. after the current active
            /// camera got disabled or removed. If none is available, the current activeCamera is left
            /// as-is (even disabled/null) rather than forced to null, since renderer code dereferences
            /// GetActiveCamera() unconditionally in several places.
            void PromoteNextActiveCamera() {
                if (activeCamera && activeCamera->Active())
                    return;

                for (auto& [id, cam] : cameras) {
                    if (cam->Active()) {
                        activeCamera = cam;
                        return;
                    }
                }
            }

            /// @brief Update all camera's sizes
            /// @param width The new width (in px)
            /// @param height The new height (in px)
            void UpdateSize(int width, int height) {
                for(std::pair<Core::ObjectID, std::shared_ptr<Camera>> cam : cameras){
                    cam.second->UpdateSize(width, height);
                }
            }

            /// @brief Update the active camera
            void Tick(){
                if(activeCamera != nullptr){
                    activeCamera->UpdateMatrix();
                }
            }

            /// @brief Get a camera by name
            std::shared_ptr<Camera> GetCamera(const Core::ObjectID parentID) {
                if (cameras.count(parentID))
                    return cameras[parentID];
                return nullptr;
            }

            /// @brief Set the active camera
            void SetActiveCamera(const Core::ObjectID parentID) {
                if (cameras.count(parentID))
                    activeCamera = cameras[parentID];
            }

            /// @brief Get the current active camera
            std::shared_ptr<Camera> GetActiveCamera() {
                return activeCamera;
            }

            /// @brief Clear all cameras
            void Clear() {
                cameras.clear();
                activeCamera = nullptr;
            }

        private:

            std::unordered_map<Core::ObjectID, std::shared_ptr<Camera>> cameras;
            std::shared_ptr<Camera> activeCamera;
    };
}