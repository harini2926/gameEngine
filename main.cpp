#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "ui/Theme.h"
#include "ui/AIAssistant.h"
#include "ui/AssetLibrary.h"
#include "ui/Dashboard.h"
#include "ui/Inspector.h"
#include "ui/ProjectPanel.h"
#include "ui/ScriptPanel.h"
#include "project/ProjectManager.h"
#include "editor/EditorState.h"
#include "editor/SceneSerializer.h"
#include "editor/ScriptEditor.h"
#include "editor/PythonRuntime.h"
#include "editor/SpriteCache.h"
#include "editor/Gizmo.h"
#include "renderer/Renderer3D.h"
#include "renderer/MeshFactory.h"
#include "renderer/BackgroundFactory.h"
#include "renderer/SceneBackdrop.h"


#include <GLFW/glfw3.h>
#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// Icons
// ============================================================
#define ICON_ROCKET   "\xE2\x9C\xA6"
#define ICON_FOLDER   "\xE2\x96\xB6"
#define ICON_OPEN     "\xE2\x96\xB7"
#define ICON_BELL     "\xE2\x97\x8F"
#define ICON_PEOPLE   "\xE2\x97\x8B"
#define ICON_BOOK     "\xE2\x96\xA0"
#define ICON_ROBOT    "\xE2\x98\x85"
#define ICON_CLOCK    "\xE2\x97\x94"
#define ICON_GAME     "\xE2\x97\x86"
#define ICON_SAVE     "\xE2\x86\x93"
#define ICON_PLAY     "\xE2\x96\xB6"
#define ICON_STOP     "\xE2\x96\xA0"
#define ICON_BACK     "\xE2\x86\x90"
#define ICON_FORWARD  "\xE2\x86\x92"
#define ICON_GEAR     "\xE2\x9A\x99"
#define ICON_GRID     "\xE2\x96\xA3"
#define ICON_SPRITE   "\xE2\x96\xA3"
#define ICON_PHYSICS  "\xE2\x9A\x99"
#define ICON_BOX      "\xE2\x96\xA0"

// ============================================================
// Globals
// ============================================================
static NexoraRenderer::Renderer3D g_renderer3D;
static NexoraEditor::GizmoState    g_gizmoState;
static NexoraUI::AIAssistant       g_aiAssistant;

// ============================================================
// View
// ============================================================
enum class View
{
    Landing, Login, Register, Dashboard,
    CreateProject, OpenProject, Editor
};

struct RecentEntry
{
    std::string name, type, path;
    RecentEntry() = default;
    RecentEntry(std::string n, std::string t, std::string p)
        : name(std::move(n)), type(std::move(t)), path(std::move(p)) {}
};

// ============================================================
// Helpers
// ============================================================
static inline float S() { return NexoraTheme::Scale(); }

static float BtnHeight(const char* label, float padY = 20.0f)
{
    ImGui::PushFont(NexoraTheme::Medium());
    float h = ImGui::CalcTextSize(label).y + padY * 2.0f * S();
    ImGui::PopFont();
    return h;
}

static float BtnWidth(const char* label, float padX = 32.0f)
{
    ImGui::PushFont(NexoraTheme::Medium());
    float w = ImGui::CalcTextSize(label).x + padX * 2.0f * S();
    ImGui::PopFont();
    return w;
}

static void CopyToBuf(char* buf, size_t n, const std::string& s)
{
    if (!buf || n == 0) return;
    size_t len = s.size();
    if (len > n - 1) len = n - 1;
    if (len > 0) std::memcpy(buf, s.data(), len);
    buf[len] = '\0';
}

static std::string DefaultProjectsDir()
{
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (ec) return "Projects";
    return (cwd / "Projects").string();
}

static void TryAutoLoadMainScene(NexoraEditor::EditorState& ed)
{
    std::error_code ec;
    fs::path p = fs::path(ed.projectPath) / "Scenes" / "Main.scene";
    if (fs::exists(p, ec))
        NexoraEditor::LoadScene(ed, p.string());
}

static ImU32 ObjectColor(NexoraEditor::SceneObject const& o)
{
    switch (o.colorIndex % 8)
    {
        case 0: return IM_COL32( 90, 130, 230, 255);
        case 1: return IM_COL32(230, 140,  90, 255);
        case 2: return IM_COL32( 90, 200, 120, 255);
        case 3: return IM_COL32(220, 190,  90, 255);
        case 4: return IM_COL32(170, 120, 220, 255);
        case 5: return IM_COL32(230, 110, 160, 255);
        case 6: return IM_COL32(110, 200, 220, 255);
        default: return IM_COL32(220, 100, 100, 255);
    }
}

// ============================================================
// Viewport
// ============================================================
static void DrawViewportPanel(NexoraEditor::EditorState& ed,
                              float width, float height)
{
    using namespace NexoraTheme::Color;
    const float s = S();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.040f, 0.046f, 0.072f, 1.0f));
    ImGui::BeginChild("##ViewportPanel", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar);

    // Header
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(16 * s);
    ImGui::PushFont(NexoraTheme::Medium());
    ImGui::PushStyleColor(ImGuiCol_Text, Text);
    ImGui::TextUnformatted(ICON_GAME "  Scene");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine(0, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    if (ed.currentScenePath.empty())
        ImGui::TextUnformatted("(unsaved)");
    else
        ImGui::TextUnformatted(
            fs::path(ed.currentScenePath).filename().string().c_str());
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(12 * s);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8 * s));

    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y -= 20 * s;
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;

    const bool is3DProject = (ed.projectType.find("3D") != std::string::npos);

    ImGui::InvisibleButton("##3DCanvas", ImVec2(canvasSize.x, canvasSize.y),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);

    // Drop target
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("NEXORA_ASSET"))
        {
            const char* raw = static_cast<const char*>(payload->Data);
            std::string packed = raw ? raw : "";

            std::string file, name, is3DStr;
            size_t p1 = packed.find('|');
            if (p1 != std::string::npos)
            {
                file = packed.substr(0, p1);
                size_t p2 = packed.find('|', p1 + 1);
                if (p2 != std::string::npos)
                {
                    is3DStr = packed.substr(p1 + 1, p2 - p1 - 1);
                    name    = packed.substr(p2 + 1);
                }
            }
            bool is3D = (is3DStr == "1") || is3DProject;
            if (name.empty()) name = "Asset";

            if (file.rfind("Backgrounds/", 0) != 0)
            {
                NexoraEditor::SceneObject& newObj =
                    ed.AddObjectEx(name, "Asset", 0.0f, 0.5f, 0.0f, is3D);
                newObj.image = file;
                ed.SelectOnly(newObj.id);
                ed.console.Add(NexoraEditor::LogLevel::Success,
                               "Spawned " + name);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool isHovered = ImGui::IsItemHovered();

    // Camera input
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::SetWindowFocus();

    if (isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        ImGuiIO& io = ImGui::GetIO();
        ed.cameraYaw   += io.MouseDelta.x * 0.15f;
        ed.cameraPitch -= io.MouseDelta.y * 0.15f;
        if (ed.cameraPitch >  89.0f) ed.cameraPitch =  89.0f;
        if (ed.cameraPitch < -89.0f) ed.cameraPitch = -89.0f;

        float speed = ed.cameraSpeed * io.DeltaTime;

        glm::vec3 front;
        front.x = cos(glm::radians(ed.cameraYaw)) * cos(glm::radians(ed.cameraPitch));
        front.y = sin(glm::radians(ed.cameraPitch));
        front.z = sin(glm::radians(ed.cameraYaw)) * cos(glm::radians(ed.cameraPitch));
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));

        GLFWwindow* w = glfwGetCurrentContext();
        if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) { ed.cameraX += front.x * speed; ed.cameraY += front.y * speed; ed.cameraZ += front.z * speed; }
        if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) { ed.cameraX -= front.x * speed; ed.cameraY -= front.y * speed; ed.cameraZ -= front.z * speed; }
        if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) { ed.cameraX -= right.x * speed; ed.cameraZ -= right.z * speed; }
        if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) { ed.cameraX += right.x * speed; ed.cameraZ += right.z * speed; }
        if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) { ed.cameraY -= speed; }
        if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) { ed.cameraY += speed; }
    }

    if (isHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        float w = ImGui::GetIO().MouseWheel;
        ed.cameraSpeed += w * 2.0f;
        if (ed.cameraSpeed < 1.0f)   ed.cameraSpeed = 1.0f;
        if (ed.cameraSpeed > 100.0f) ed.cameraSpeed = 100.0f;
    }

    // ---- Camera matrices ----
    glm::mat4 view, proj;

    if (is3DProject)
    {
        // Unity-style: low camera, looking slightly down
        glm::vec3 camPos(ed.cameraX, ed.cameraY, ed.cameraZ);

        // Compute forward direction from yaw/pitch
        float yawRad = glm::radians(ed.cameraYaw);
        float pitchRad = glm::radians(ed.cameraPitch);
        glm::vec3 forward(
            cos(pitchRad) * cos(yawRad),
            sin(pitchRad),
            cos(pitchRad) * sin(yawRad)
        );

        glm::vec3 camTarget = camPos + forward;

        view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));

        proj = glm::perspective(
            glm::radians(60.0f),
            (std::max)(1.0f, canvasSize.x) / (std::max)(1.0f, canvasSize.y),
            0.1f, 2000.0f);
    }
    else
    {
        // 2D orthographic top-down
        float halfH = canvasSize.y * 0.03f;
        float halfW = halfH * (canvasSize.x / canvasSize.y);
        view = glm::lookAt(
            glm::vec3(0, 50, 0),
            glm::vec3(0, 0, 0),
            glm::vec3(0, 0, -1));
        proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.1f, 100.0f);
    }

    // ---- Render 3D scene ----
    g_renderer3D.SetClearColor(
        is3DProject ? 0.13f : 0.13f,
        is3DProject ? 0.15f : 0.15f,
        is3DProject ? 0.19f : 0.19f);
    g_renderer3D.BeginFrame((int)canvasSize.x, (int)canvasSize.y);

    // Draw 3D objects
    for (auto& o : ed.sceneObjects)
    {
        if (!o.is3D) continue;

        glm::vec3 pos(o.x, o.y, o.z);
        glm::vec3 color = (o.colorIndex == 2)
            ? glm::vec3(0.35f, 0.75f, 0.45f)
            : glm::vec3(0.55f, 0.60f, 0.85f);

        NexoraRenderer::Mesh* mesh = nullptr;
        std::string modelPath = NexoraRenderer::GetModelPath(ed.projectPath, o.name);
        if (!modelPath.empty() && fs::exists(modelPath))
            mesh = g_renderer3D.GetCachedMesh(modelPath);

        if (!mesh)
        {
            fs::path cubePath = fs::temp_directory_path() / "nexora_engine" / "cube.obj";
            if (fs::exists(cubePath))
                mesh = g_renderer3D.GetCachedMesh(cubePath.string());
        }

        if (mesh)
            g_renderer3D.DrawMesh(*mesh, pos, o.scaleX, o.rotationY,
                                  view, proj, color);
    }

    g_renderer3D.EndFrame();

    // Paint FBO
    unsigned int gameTex = g_renderer3D.GetFramebufferTexture();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (gameTex != 0)
        dl->AddImage((ImTextureID)(intptr_t)gameTex, canvasPos,
                     ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                     ImVec2(0, 1), ImVec2(1, 0));
    else
        dl->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(33, 38, 48, 255));

    // ---- 3D Grid (Unity-style with horizon) ----
    if (is3DProject)
    {
        const int   gridLines = 50;    // 50 units each direction
        const float gridStep  = 1.0f;

        // Project a 3D point to 2D screen
        auto ProjectToScreen = [&](const glm::vec3& worldPos, bool& ok) -> ImVec2
        {
            glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.01f) { ok = false; return ImVec2(0, 0); }
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            ok = true;
            float sx = canvasPos.x + (ndc.x * 0.5f + 0.5f) * canvasSize.x;
            float sy = canvasPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * canvasSize.y;
            return ImVec2(sx, sy);
        };

        // Fade based on distance from origin
        auto FadeForDist = [&](float dist) -> float
        {
            return std::max(0.05f, 1.0f - dist / (float)gridLines);
        };

        // Draw X-axis lines (parallel to X, spaced along Z)
        for (int i = -gridLines; i <= gridLines; ++i)
        {
            float z = i * gridStep;

            bool okA, okB;
            ImVec2 a = ProjectToScreen(glm::vec3(-gridLines, 0, z), okA);
            ImVec2 b = ProjectToScreen(glm::vec3( gridLines, 0, z), okB);
            if (!okA || !okB) continue;

            // If line is a full axis (i == 0), don't fade
            if (i == 0)
            {
                dl->AddLine(a, b, IM_COL32(90, 105, 130, 255), 2.0f);
            }
            else
            {
                float fade = FadeForDist(std::abs(z));
                ImU32 col = IM_COL32(70, 85, 110, (int)(160 * fade));
                dl->AddLine(a, b, col, 1.0f);
            }
        }

        // Draw Z-axis lines (parallel to Z, spaced along X)
        for (int i = -gridLines; i <= gridLines; ++i)
        {
            float x = i * gridStep;

            bool okA, okB;
            ImVec2 a = ProjectToScreen(glm::vec3(x, 0, -gridLines), okA);
            ImVec2 b = ProjectToScreen(glm::vec3(x, 0,  gridLines), okB);
            if (!okA || !okB) continue;

            if (i == 0)
            {
                dl->AddLine(a, b, IM_COL32(90, 105, 130, 255), 2.0f);
            }
            else
            {
                float fade = FadeForDist(std::abs(x));
                ImU32 col = IM_COL32(70, 85, 110, (int)(160 * fade));
                dl->AddLine(a, b, col, 1.0f);
            }
        }

        // Red X-axis (the +X direction)
        {
            bool okA, okB;
            ImVec2 o = ProjectToScreen(glm::vec3(0, 0, 0), okA);
            ImVec2 e = ProjectToScreen(glm::vec3(5, 0, 0), okB);
            if (okA && okB)
                dl->AddLine(o, e, IM_COL32(220, 100, 100, 255), 3.0f);
        }

        // Green Z-axis (the +Z direction)
        {
            bool okA, okB;
            ImVec2 o = ProjectToScreen(glm::vec3(0, 0, 0), okA);
            ImVec2 e = ProjectToScreen(glm::vec3(0, 0, 5), okB);
            if (okA && okB)
                dl->AddLine(o, e, IM_COL32(100, 220, 120, 255), 3.0f);
        }
    }
    else
    {
        // ---- 2D Grid (top-down orthographic) ----
        const int   gridLines = 30;
        const float gridStep  = 1.0f;

        auto WorldToScreen2D = [&](float wx, float wy) -> ImVec2
        {
            float halfH = canvasSize.y * 0.03f;
            float halfW = halfH * (canvasSize.x / canvasSize.y);
            float sx = canvasPos.x + ((wx + halfW) / (2 * halfW)) * canvasSize.x;
            float sy = canvasPos.y + ((wy + halfH) / (2 * halfH)) * canvasSize.y;
            return ImVec2(sx, sy);
        };

        for (int i = -gridLines; i <= gridLines; ++i)
        {
            float wx = i * gridStep;
            float fade = std::max(0.15f, 1.0f - std::abs(wx) / gridLines);

            ImU32 col = (i == 0) ? IM_COL32(120, 140, 180, 220)
                                 : IM_COL32(70, 85, 110, (int)(180 * fade));

            ImVec2 a = WorldToScreen2D(wx, -gridLines * gridStep);
            ImVec2 b = WorldToScreen2D(wx,  gridLines * gridStep);
            dl->AddLine(a, b, col, (i == 0) ? 2.0f : 1.0f);
        }

        for (int i = -gridLines; i <= gridLines; ++i)
        {
            float wy = i * gridStep;
            float fade = std::max(0.15f, 1.0f - std::abs(wy) / gridLines);

            ImU32 col = (i == 0) ? IM_COL32(120, 140, 180, 220)
                                 : IM_COL32(70, 85, 110, (int)(180 * fade));

            ImVec2 a = WorldToScreen2D(-gridLines * gridStep, wy);
            ImVec2 b = WorldToScreen2D( gridLines * gridStep, wy);
            dl->AddLine(a, b, col, (i == 0) ? 2.0f : 1.0f);
        }

        ImVec2 origin = WorldToScreen2D(0, 0);
        ImVec2 xEnd   = WorldToScreen2D(5, 0);
        ImVec2 yEnd   = WorldToScreen2D(0, 5);
        dl->AddLine(origin, xEnd, IM_COL32(220, 100, 100, 255), 2.5f);
        dl->AddLine(origin, yEnd, IM_COL32(100, 220, 120, 255), 2.5f);

        // Draw 2D sprites
        for (auto& o : ed.sceneObjects)
        {
            if (o.is3D) continue;

            ImVec2 p0 = WorldToScreen2D(o.x - o.scaleX * 0.5f, o.z - o.scaleZ * 0.5f);
            ImVec2 p1 = WorldToScreen2D(o.x + o.scaleX * 0.5f, o.z + o.scaleZ * 0.5f);

            ImTextureID tex = nullptr;
            if (!o.image.empty())
                tex = NexoraEditor::GetSpriteTexture(ed.projectPath, o.image);

            if (tex)
                dl->AddImage(tex, p0, p1);
            else
                dl->AddRectFilled(p0, p1, IM_COL32(90, 130, 220, 255), 4 * s);

            dl->AddText(ImVec2(p0.x, p1.y + 2),
                        IM_COL32(255, 255, 255, 220), o.name.c_str());
        }
    }

    // HUD
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "Camera: X:%.1f Y:%.1f Z:%.1f | Speed: %.0f | Mode: %s",
                      ed.cameraX, ed.cameraY, ed.cameraZ, ed.cameraSpeed,
                      is3DProject ? "3D" : "2D");
        dl->AddText(ImVec2(canvasPos.x + 16 * s, canvasPos.y + 12 * s),
                    IM_COL32(200, 220, 255, 240), buf);
    }
    {
        const char* hint = is3DProject
            ? "Right-click + WASD to fly | Q/E to go up/down | Scroll: speed"
            : "Top-down 2D view | Right-click + WASD to pan | Scroll: speed";
        dl->AddText(ImVec2(canvasPos.x + 16 * s,
                           canvasPos.y + canvasSize.y - 20 * s),
                    IM_COL32(180, 190, 220, 240), hint);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
// ============================================================
static void DrawEditorMenuBar(NexoraEditor::EditorState& ed, bool& wantClose)
{
    using namespace NexoraTheme::Color;
    const float s = S();

    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.070f, 0.078f, 0.108f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24 * s, 14 * s));

    if (ImGui::BeginMenuBar())
    {
        ImGui::PushFont(NexoraTheme::Medium());
        ImGui::PushStyleColor(ImGuiCol_Text, Accent);
        ImGui::TextUnformatted(ICON_ROCKET "  NEXORA");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::SameLine(0, 40 * s);

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                std::string target = ed.currentScenePath;
                if (target.empty())
                    target = (fs::path(ed.projectPath) / "Scenes" / "Main.scene").string();
                if (NexoraEditor::SaveScene(ed, target))
                {
                    ed.currentScenePath = target;
                    ed.console.Add(NexoraEditor::LogLevel::Success, "Scene saved");
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) wantClose = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) ed.Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) ed.Redo();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ============================================================
// Play tick
// ============================================================
static void UpdatePlayMode(NexoraEditor::EditorState& ed)
{
    if (!ed.playing) return;
    ImGuiIO& io = ImGui::GetIO();
    ed.playTime += io.DeltaTime;
}

// ============================================================
// Input pump
// ============================================================
static void PumpInput(GLFWwindow* window)
{
    bool a  = glfwGetKey(window, GLFW_KEY_A)     == GLFW_PRESS;
    bool d  = glfwGetKey(window, GLFW_KEY_D)     == GLFW_PRESS;
    bool w  = glfwGetKey(window, GLFW_KEY_W)     == GLFW_PRESS;
    bool s  = glfwGetKey(window, GLFW_KEY_S)     == GLFW_PRESS;
    bool sp = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    bool l  = glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS;
    bool r  = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
    bool u  = glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS;
    bool dn = glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS;

    NexoraEditor::FeedInputState(a, d, w, s, sp, l, r, u, dn);
}

// ============================================================
// Editor layout (exactly matches the ASCII diagram)
//
//  ┌──────────────────────────────────────────────────────┐
//  │  NEXORA   File  Edit                                 │
//  ├──────────────────────────────────────────────────────┤
//  │  Project: MyNewGame       [Save Scene] [▶ Play]      │
//  ├──────────────┬────────────────────┬──────────────────┤
//  │  Project     │     Viewport       │    Inspector     │
//  │  Tree        │   (grid + gizmo)   │   (with expand)  │
//  ├──────────────┴────────────────────┴──────────────────┤
//  │  Script Editor               │   AI Assistant        │
//  │  [Save][Save As][Rename]…    │                       │
//  │  ┌─────┬────────────────┐    │   Chat with Ollama    │
//  │  │Tree │  Code editor   │    │                       │
//  │  └─────┴────────────────┘    │                       │
//  └──────────────────────────────┴───────────────────────┘
// ============================================================
static void DrawEditorView(NexoraEditor::EditorState& ed,
                           float W, float H,
                           bool& wantClose)
{
    const float s = S();

    // ---- Vertical partition ----
    const float menuBarH   = 48 * s;
    const float playStripH = 64 * s;
    const float topH       = menuBarH + playStripH;

    // Bottom (Script + AI) : 42% normal, 78% when expanded
    const float bottomH = ed.scriptExpanded
        ? (H * 0.65f)
        : (H * 0.30f);  

    const float middleH = H - topH - bottomH - 6 * s;

    // ---- Horizontal partition (middle row) ----
    static bool s_inspectorExpanded = false;

    float leftW  = 260.0f * s;
    float rightW = s_inspectorExpanded ? (W * 0.42f) : (340.0f * s);

    const float minCenter = 400.0f * s;
    if (leftW + rightW + minCenter > W)
    {
        float excess = (leftW + rightW + minCenter) - W;
        leftW  -= excess * 0.5f;
        rightW -= excess * 0.5f;
        if (leftW  < 200 * s) leftW  = 200 * s;
        if (rightW < 320 * s) rightW = 320 * s;
    }
    const float centerW = W - leftW - rightW;

    // ---- Horizontal partition (bottom row) ----
    

    // ============================================================
    // Row 1: menu bar
    // ============================================================
    ImGui::BeginChild("##MenuBar", ImVec2(0, menuBarH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar);
    DrawEditorMenuBar(ed, wantClose);
    ImGui::EndChild();

    // ============================================================
    // Row 2: play strip
    // ============================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.070f, 0.078f, 0.108f, 1.0f));
    ImGui::BeginChild("##PlayStrip", ImVec2(0, playStripH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::SetCursorPos(ImVec2(24 * s,
            (playStripH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushFont(NexoraTheme::Medium());
        ImGui::Text("Project: %s", ed.projectName.c_str());
        ImGui::PopFont();

        if (ed.playing)
        {
            ImGui::SameLine(0, 30 * s);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 0.50f, 1.0f));
            ImGui::PushFont(NexoraTheme::Medium());
            ImGui::Text("\xE2\x96\xB6  PLAYING  \xC2\xB7  %.1f s", ed.playTime);
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }

        const float btnH  = 40 * s;
        const float saveW = BtnWidth(ICON_SAVE "  Save Scene", 20.0f);
        const float playW = BtnWidth(ICON_PLAY "  Play",       20.0f);
        const float stopW = BtnWidth(ICON_STOP "  Stop",       20.0f);
        const float gap   = 10 * s;
        const float activeW = ed.playing ? stopW : playW;
        const float totalW = saveW + activeW + gap;

        ImVec2 childPos = ImGui::GetWindowPos();
        float childW    = ImGui::GetWindowWidth();
        float rightX    = childW - totalW - 40 * s;
        if (rightX < 300 * s) rightX = 300 * s;
        ImGui::SetCursorPos(ImVec2(rightX, (playStripH - btnH) * 0.5f));

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 12 * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * s);

        // ---- Save Scene ----
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.82f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.32f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
        ImGui::BeginDisabled(ed.playing);
        if (ImGui::Button(ICON_SAVE "  Save Scene", ImVec2(saveW, btnH)))
        {
            std::string target = ed.currentScenePath;
            if (target.empty())
                target = (fs::path(ed.projectPath) / "Scenes" / "Main.scene").string();
            if (NexoraEditor::SaveScene(ed, target))
            {
                ed.currentScenePath = target;
                ed.console.Add(NexoraEditor::LogLevel::Success, "Scene saved");
            }
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0, gap);

        // ---- Play / Stop ----
        if (ed.playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.32f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
            if (ImGui::Button(ICON_STOP "  Stop", ImVec2(stopW, btnH)))
            {
                NexoraEditor::StopPlayScripts();
                ed.StopPlay();
            }
            ImGui::PopStyleColor(4);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.75f, 0.44f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.55f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
            if (ImGui::Button(ICON_PLAY "  Play", ImVec2(playW, btnH)))
            {
                ed.StartPlay();
                NexoraEditor::SetLiveEditor(&ed);
                NexoraEditor::StartPlayScripts(ed.sceneObjects, ed.projectPath);
            }
            ImGui::PopStyleColor(4);
        }

        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ============================================================
    // Row 3: middle — Project Tree | Viewport | Inspector
    // ============================================================
    NexoraUI::DrawProjectPanel(ed, leftW, middleH);
    ImGui::SameLine(0, 0);
    DrawViewportPanel(ed, centerW, middleH);
    ImGui::SameLine(0, 0);
    NexoraUI::DrawInspector(ed, rightW, middleH, s_inspectorExpanded);

    // ============================================================
    // Row 4: bottom — Script Editor | AI Assistant
    // ============================================================
    const float aiW     = 380.0f * s;    // fixed 380px for AI panel
    const float scriptW = W - aiW;        // Script Editor takes the rest

    // Draw Script Editor
    NexoraUI::DrawScriptPanel(ed, scriptW, bottomH);

    // Draw AI Assistant to the right of it
    ImGui::SameLine(0, 0);
    g_aiAssistant.Draw(ed, aiW, bottomH);
}

// ============================================================
// Dashboard
// ============================================================
static void DrawDashboardView(NexoraEditor::EditorState& editor,
                              View& currentView,
                              std::vector<RecentEntry>& recentProjects,
                              float W, float H)
{
    std::vector<NexoraUI::RecentProject> rp;
    for (auto& r : recentProjects)
        rp.push_back({ r.name, r.type, r.path });

    static NexoraUI::DashboardState s_dash;
    int action = 0;
    std::string path;

    NexoraUI::DrawDashboard(editor, s_dash, rp, W, H, action, path);

    if (action == 1)
    {
        currentView = View::CreateProject;
    }
    else if (action == 2)
    {
        currentView = View::OpenProject;
    }
    else if (action == 3)
    {
        NexoraEditor::ClearSpriteCache();
        g_renderer3D.ClearCache();

        std::string name = fs::path(path).filename().string();
        editor.OpenProject(name, "Unknown", path);
        NexoraRenderer::EnsureBuiltInModels(editor.projectPath);
        NexoraRenderer::EnsureBuiltInBackgrounds(editor.projectPath);
        currentView = View::Editor;
        TryAutoLoadMainScene(editor);
    }
}

// ============================================================
// Create project view
// ============================================================
static void DrawCreateProjectView(NexoraEditor::EditorState& editor,
                                  View& currentView,
                                  std::vector<RecentEntry>& recentProjects,
                                  float W, float H)
{
    using namespace NexoraTheme::Color;
    const float s = S();

    static char newName[128] = "MyNewGame";
    static char newLoc[260]  = "";
    static int  typeIndex    = 0;
    const char* types[] = { "2D Game", "3D Game", "Visual Novel", "AI Interactive" };

    if (newLoc[0] == '\0')
    {
        std::string def = DefaultProjectsDir();
        CopyToBuf(newLoc, sizeof(newLoc), def);
    }

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(W, H),
        IM_COL32(12, 14, 24, 255), IM_COL32(22, 24, 44, 255),
        IM_COL32(8, 10, 18, 255), IM_COL32(14, 16, 28, 255));

    const float padX = 80 * s;

    ImGui::Dummy(ImVec2(0, 60 * s));

    float backH = BtnHeight(ICON_BACK "  Back", 18.0f);
    float backW = BtnWidth(ICON_BACK "  Back", 30.0f);

    ImGui::SetCursorPosX(padX);
    ImGui::PushStyleColor(ImGuiCol_Button,        PanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PanelHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  PanelAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 18 * s));
    if (ImGui::Button(ICON_BACK "  Back", ImVec2(backW, backH)))
        currentView = View::Dashboard;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(0, 40 * s));

    ImGui::SetCursorPosX(padX);
    ImGui::PushFont(NexoraTheme::Title());
    ImGui::PushStyleColor(ImGuiCol_Text, Accent);
    ImGui::TextUnformatted(ICON_ROCKET "  Create New Project");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 40 * s));

    const float availW = ImGui::GetContentRegionAvail().x - padX * 2.0f;
    const float formW  = availW * 0.62f;

    ImGui::SetCursorPosX(padX);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
    ImGui::BeginChild("##createForm", ImVec2(formW, 560 * s), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    const float inner = 44 * s;
    ImGui::Dummy(ImVec2(0, inner));

    ImGui::SetCursorPosX(inner);
    ImGui::PushFont(NexoraTheme::Medium());
    ImGui::TextUnformatted("Project Name");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::SetNextItemWidth(formW - inner * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 16 * s));
    ImGui::InputText("##name", newName, 128);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, 32 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::PushFont(NexoraTheme::Medium());
    ImGui::TextUnformatted("Project Type");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::SetNextItemWidth(formW - inner * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 16 * s));
    ImGui::Combo("##type", &typeIndex, types, 4);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, 32 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::PushFont(NexoraTheme::Medium());
    ImGui::TextUnformatted("Location");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::SetNextItemWidth(formW - inner * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 16 * s));
    ImGui::InputText("##loc", newLoc, 260);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, 56 * s));

    float cancelH = BtnHeight("Cancel", 20.0f);
    float cancelW = BtnWidth("Cancel", 30.0f);
    float createH = BtnHeight("Create Project  " ICON_FORWARD, 20.0f);
    float createW = BtnWidth("Create Project  " ICON_FORWARD, 30.0f);

    ImGui::SetCursorPosX(inner);
    ImGui::PushStyleColor(ImGuiCol_Button,        PanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PanelHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  PanelAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 20 * s));
    if (ImGui::Button("Cancel", ImVec2(cancelW, cancelH)))
        currentView = View::Dashboard;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 16 * s);

    ImGui::PushStyleColor(ImGuiCol_Button,        Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AccentDim);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 20 * s));
    if (ImGui::Button("Create Project  " ICON_FORWARD, ImVec2(createW, createH)))
    {
        NexoraProject::ProjectInfo info;
        if (NexoraProject::CreateProject(newName, types[typeIndex], newLoc, info))
        {
            recentProjects.insert(recentProjects.begin(),
                RecentEntry(info.name, info.type, info.path));
            NexoraEditor::ClearSpriteCache();
            g_renderer3D.ClearCache();
            editor.OpenProject(info.name, info.type, info.path);
            NexoraRenderer::EnsureBuiltInModels(editor.projectPath);
            NexoraRenderer::EnsureBuiltInBackgrounds(editor.projectPath);
            currentView = View::Editor;
            TryAutoLoadMainScene(editor);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
// Open project view
// ============================================================
static void DrawOpenProjectView(NexoraEditor::EditorState& editor,
                                View& currentView,
                                std::vector<RecentEntry>& recentProjects,
                                float W, float H)
{
    using namespace NexoraTheme::Color;
    const float s = S();

    static char openPath[260] = "";

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(W, H),
        IM_COL32(12, 14, 24, 255), IM_COL32(22, 24, 44, 255),
        IM_COL32(8, 10, 18, 255), IM_COL32(14, 16, 28, 255));

    const float padX = 80 * s;

    ImGui::Dummy(ImVec2(0, 60 * s));

    float backH = BtnHeight(ICON_BACK "  Back", 18.0f);
    float backW = BtnWidth(ICON_BACK "  Back", 30.0f);

    ImGui::SetCursorPosX(padX);
    ImGui::PushStyleColor(ImGuiCol_Button,        PanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PanelHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  PanelAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 18 * s));
    if (ImGui::Button(ICON_BACK "  Back", ImVec2(backW, backH)))
        currentView = View::Dashboard;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(0, 40 * s));

    ImGui::SetCursorPosX(padX);
    ImGui::PushFont(NexoraTheme::Title());
    ImGui::PushStyleColor(ImGuiCol_Text, Accent);
    ImGui::TextUnformatted(ICON_OPEN "  Open Project");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 40 * s));

    const float availW = ImGui::GetContentRegionAvail().x - padX * 2.0f;
    const float formW  = availW * 0.62f;

    ImGui::SetCursorPosX(padX);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
    ImGui::BeginChild("##openForm", ImVec2(formW, 320 * s), true);

    const float inner = 44 * s;
    ImGui::Dummy(ImVec2(0, inner));

    ImGui::SetCursorPosX(inner);
    ImGui::PushFont(NexoraTheme::Medium());
    ImGui::TextUnformatted("Project Folder Path");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(inner);
    ImGui::SetNextItemWidth(formW - inner * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 16 * s));
    ImGui::InputText("##openpath", openPath, sizeof(openPath));
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, 40 * s));

    ImGui::SetCursorPosX(inner);
    float cancelH = BtnHeight("Cancel", 20.0f);
    float cancelW = BtnWidth("Cancel", 30.0f);
    float openH   = BtnHeight("Open  " ICON_FORWARD, 20.0f);
    float openW   = BtnWidth("Open  " ICON_FORWARD, 30.0f);

    ImGui::PushStyleColor(ImGuiCol_Button,        PanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PanelHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  PanelAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 20 * s));
    if (ImGui::Button("Cancel", ImVec2(cancelW, cancelH)))
        currentView = View::Dashboard;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 16 * s);

    ImGui::PushStyleColor(ImGuiCol_Button,        Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AccentDim);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, 20 * s));
    if (ImGui::Button("Open  " ICON_FORWARD, ImVec2(openW, openH)))
    {
        std::error_code ec;
        fs::path p(openPath);
        if (fs::exists(p, ec))
        {
            std::string name = p.filename().string();
            recentProjects.insert(recentProjects.begin(),
                RecentEntry(name, "Unknown", p.string()));
            NexoraEditor::ClearSpriteCache();
            g_renderer3D.ClearCache();
            editor.OpenProject(name, "Unknown", p.string());
            NexoraRenderer::EnsureBuiltInModels(editor.projectPath);
            NexoraRenderer::EnsureBuiltInBackgrounds(editor.projectPath);
            currentView = View::Editor;
            TryAutoLoadMainScene(editor);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    if (!glfwInit())
    {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1600, 900,
        "Nexora Game Engine", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }

    // Fill the monitor work area
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
    {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
        glfwSetWindowPos(window, mx, my);
        glfwSetWindowSize(window, mw, mh);
    }
    glfwMaximizeWindow(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "GLAD init failed\n";
        glfwTerminate();
        return -1;
    }

    // Flush the resize event so ImGui sees the correct size on frame 1
    glfwPollEvents();
    glfwPollEvents();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    
        // Delete any stale imgui.ini so no old windows (e.g. "Debug") reappear
    {
        std::error_code ec;
        fs::remove(fs::current_path() / "imgui.ini", ec);
    }

    NexoraTheme::Apply(1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    NexoraEditor::InitPython();
    g_renderer3D.Init();

    View currentView = View::Dashboard;
    NexoraEditor::EditorState editor;
    NexoraEditor::SetLiveEditor(&editor);

    std::vector<RecentEntry> recentProjects;
    {
        auto loaded = NexoraProject::LoadRecentProjects();
        for (auto& p : loaded)
            recentProjects.emplace_back(p.name, p.type, p.path);
    }

    // NOTE: We do NOT auto-open a project. The user picks from the dashboard.

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

                // Nuke any stray "Debug##Default" window ImGui creates automatically
        {
            ImGuiContext* ctx = ImGui::GetCurrentContext();
            if (ctx)
            {
                for (int i = 0; i < ctx->Windows.Size; ++i)
                {
                    ImGuiWindow* w = ctx->Windows[i];
                    if (w && w->Name)
                    {
                        // Hide any window whose name starts with "Debug"
                        if (std::strncmp(w->Name, "Debug", 5) == 0)
                        {
                            w->Hidden = true;
                            w->HiddenFramesCannotSkipItems = 1000000;
                            w->HiddenFramesForRenderOnly = 1000000;
                        }
                    }
                }
            }
        }

      

        if (editor.playing)
        {
            PumpInput(window);
            UpdatePlayMode(editor);
            NexoraEditor::UpdatePlayScripts(editor.sceneObjects, io.DeltaTime);

            std::string py = NexoraEditor::ConsumePythonOutput();
            if (!py.empty())
            {
                std::istringstream iss(py);
                std::string line;
                while (std::getline(iss, line))
                {
                    if (line.empty()) continue;
                    auto lvl = NexoraEditor::LogLevel::Info;
                    if (line.rfind("[ERROR]", 0) == 0)
                        lvl = NexoraEditor::LogLevel::Error;
                    else if (line.rfind("[WARN]", 0) == 0)
                        lvl = NexoraEditor::LogLevel::Warning;
                    editor.console.Add(lvl, line);
                }
            }
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float W = vp->WorkSize.x;
        const float H = vp->WorkSize.y;

        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
      

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::Begin("NexoraRoot", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings );

        ImGui::PopStyleVar(3);

        bool wantCloseEditor = false;

        switch (currentView)
        {
        case View::Landing:
        case View::Login:
        case View::Register:
            // Use the Dashboard for all of these for now
            DrawDashboardView(editor, currentView, recentProjects, W, H);
            break;

        case View::Dashboard:
            DrawDashboardView(editor, currentView, recentProjects, W, H);
            break;

        case View::CreateProject:
            DrawCreateProjectView(editor, currentView, recentProjects, W, H);
            break;

        case View::OpenProject:
            DrawOpenProjectView(editor, currentView, recentProjects, W, H);
            break;

        case View::Editor:
            DrawEditorView(editor, W, H, wantCloseEditor);
            if (wantCloseEditor)
            {
                NexoraEditor::ClearSpriteCache();
                g_renderer3D.ClearCache();
                editor.CloseProject();
                currentView = View::Dashboard;
            }
            break;
        }

        ImGui::End();
        ImGui::Render();

        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.04f, 0.046f, 0.072f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    g_renderer3D.Shutdown();
    NexoraEditor::ClearSpriteCache();
    NexoraEditor::ShutdownPython();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}