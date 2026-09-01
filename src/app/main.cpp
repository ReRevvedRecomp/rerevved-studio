#include "application.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace
{

void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void DropCallback(GLFWwindow* window, int count, const char** paths)
{
    auto* application = static_cast<rerevved::studio::Application*>(
        glfwGetWindowUserPointer(window));

    for (int index = 0; index < count; ++index)
        (void)application->OpenPath(std::filesystem::path(paths[index]));
}

} // namespace

int main(int argc, char** argv)
{
    bool smoke_test = false;
    for (int index = 1; index < argc; ++index)
        smoke_test = smoke_test || std::string_view(argv[index]) == "--smoke-test";

    glfwSetErrorCallback(GlfwErrorCallback);
    if (glfwInit() == GLFW_FALSE)
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (smoke_test)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "ReRevved Studio", nullptr, nullptr);
    if (window == nullptr)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (smoke_test)
        io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
        rerevved::studio::Application application;
        glfwSetWindowUserPointer(window, &application);
        glfwSetDropCallback(window, DropCallback);
        for (int index = 1; index < argc; ++index)
        {
            if (std::string_view(argv[index]) != "--smoke-test")
                (void)application.OpenPath(std::filesystem::path(argv[index]));
        }

        int rendered_frames = 0;
        while (glfwWindowShouldClose(window) == GLFW_FALSE && !application.ShouldClose())
        {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            application.Draw();

            ImGui::Render();
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.055F, 0.063F, 0.078F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
            ++rendered_frames;
            if (smoke_test && rendered_frames == 1)
                break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
