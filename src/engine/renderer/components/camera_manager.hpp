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
            
            void AddCamera(const Core::ObjectID parentID, std::shared_ptr<Camera> camera) {
                cameras[parentID] = camera;

                if (!activeCamera)
                    activeCamera = camera;
            }

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

            // If no camera is enabled, activeCamera is left as is (even disabled/null) because renderer code dereferences GetActiveCamera() unconditionally
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

            void UpdateSize(int width, int height) {
                for(std::pair<Core::ObjectID, std::shared_ptr<Camera>> cam : cameras){
                    cam.second->UpdateSize(width, height);
                }
            }

            void Tick(){
                if(activeCamera != nullptr){
                    activeCamera->UpdateMatrix();
                }
            }

            std::shared_ptr<Camera> GetCamera(const Core::ObjectID parentID) {
                if (cameras.count(parentID))
                    return cameras[parentID];
                return nullptr;
            }

            void SetActiveCamera(const Core::ObjectID parentID) {
                if (cameras.count(parentID))
                    activeCamera = cameras[parentID];
            }

            std::shared_ptr<Camera> GetActiveCamera() {
                return activeCamera;
            }

            void Clear() {
                cameras.clear();
                activeCamera = nullptr;
            }

        private:

            std::unordered_map<Core::ObjectID, std::shared_ptr<Camera>> cameras;
            std::shared_ptr<Camera> activeCamera;
    };
}