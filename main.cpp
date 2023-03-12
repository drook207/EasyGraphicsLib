#include "imgui.h"

#include <memory>
#include "window.h"

static void onUpdateCallback();

// Main code
int main(int, char **) {
    std::shared_ptr<engine::window> myWindow = std::make_shared<engine::window>(500, 500);
    myWindow->registerOnUpdateCallback(onUpdateCallback);
    if (myWindow->create() == 0) {
        myWindow->update();

    }
    myWindow->cleanup();

    return 0;
}

static void onUpdateCallback() {
    auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    ImGui::Begin("Button 1", nullptr, flags);

    ImGui::Button("Press me!");
    ImGui::End();


    ImGui::Begin("Button 2", nullptr, flags);
    ImGui::Button("Press me!");
    ImGui::End();
}
