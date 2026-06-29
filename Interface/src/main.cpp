#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <memory>

#include "theme.h"
#include "backend.h"
#include "simulator.h"
#include "widgets.h"
#include "views.h"

static AppState        g_state;
static RFIDSimulator* g_sim = nullptr;
static int             g_activeView = 0;
static bool            g_termAutoScroll = true;

static void GLFWErrorCallback(int err, const char* desc)
{
    fprintf(stderr, "[GLFW] Erro %d: %s\n", err, desc);
}

static void DrawTopBar(const FrameSnapshot& snap, float winW)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p       = ImGui::GetCursorScreenPos();

    dl->AddRectFilled({0, p.y - PADDING},
                      {winW, p.y + TOPBAR_H - PADDING},
                      U32_BG_PANEL);
    dl->AddLine({0, p.y + TOPBAR_H - PADDING - 1.0f},
                {winW, p.y + TOPBAR_H - PADDING - 1.0f},
                U32_BORDER, 1.0f);

    const char* logo = "◈";
    ImFont* f    = ImGui::GetFont();
    float   fscl = 1.4f;
    ImVec2  lsz  = f->CalcTextSizeA(f->LegacySize * fscl, 1e9f, 0, logo);
    float   t    = static_cast<float>(ImGui::GetTime());
    
    ImU32 logoCol = (snap.lastResult != AccessResult::None)
        ? (ImU32)(U32_CYAN & 0x00FFFFFFu) | (ImU32)((int)(180 + 75 * sinf(t*4)) << 24)
        : U32_CYAN;
    dl->AddText(f, f->LegacySize * fscl,
                {p.x + 8.0f, p.y + (TOPBAR_H - PADDING - lsz.y) * 0.5f},
                logoCol, logo);

    const char* title    = "RFID ACCESS CONTROL";
    const char* subtitle = "STM32F103C8T6 + MFRC522 — Bare-Metal C";
    ImVec2 titleSz = f->CalcTextSizeA(f->LegacySize * 1.15f, 1e9f, 0, title);
    float   txtX    = p.x + lsz.x + 20.0f;
    float   txtY0   = p.y + (TOPBAR_H - PADDING - titleSz.y * 2.2f) * 0.5f;
    dl->AddText(f, f->LegacySize * 1.15f, {txtX, txtY0}, U32_TEXT_HI, title);
    dl->AddText({txtX, txtY0 + titleSz.y + 1.0f}, U32_TEXT_DIM, subtitle);

    float ix = winW - 220.0f;
    float iy = p.y + (TOPBAR_H - PADDING) * 0.5f;

    bool conn = snap.connStatus == ConnectionStatus::Connected;
    ImU32 connC = conn ? U32_GREEN : U32_RED;
    ImU32 connD = conn ? U32_GREEN_DIM : U32_RED_DIM;
    DrawLED(dl, {ix, iy}, 5.0f, connC, connD, conn);
    dl->AddText({ix + 12.0f, iy - 7.0f}, U32_TEXT_DIM, "STM32");

    ix += 70.0f;
    DrawLED(dl, {ix, iy}, 5.0f, U32_CYAN, U32_CYAN_DIM, conn);
    dl->AddText({ix + 12.0f, iy - 7.0f}, U32_TEXT_DIM, "RC522");

    ix += 70.0f;
    bool tagActive = snap.lastResult != AccessResult::None;
    ImU32 tagC = tagActive ? U32_AMBER : U32_TEXT_DIM;
    DrawLED(dl, {ix, iy}, 5.0f, tagC, U32_BG_WIDGET, tagActive);
    dl->AddText({ix + 12.0f, iy - 7.0f}, U32_TEXT_DIM, "TAG");

    ImGui::Dummy({winW, TOPBAR_H - PADDING});
}

static void DrawSidebar(const FrameSnapshot& snap, float height)
{
    ImGui::BeginChild("##sidebar", {SIDEBAR_W, height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp      = ImGui::GetWindowPos();

    dl->AddRectFilled(sp, {sp.x + SIDEBAR_W, sp.y + height},
                      IM_COL32(13, 19, 27, 255));
    dl->AddLine({sp.x + SIDEBAR_W - 1, sp.y},
                {sp.x + SIDEBAR_W - 1, sp.y + height},
                U32_BORDER, 1.0f);

    ImGui::Spacing();

    struct MenuItem { const char* icon; const char* label; int view; };
    static const MenuItem items[] = {
        {"⬡", "Monitoramento", 0},
        {"⬡", "Configurações", 1},
        {"⬡", "Histórico",     2},
    };

    for (auto& item : items)
    {
        bool active = (g_activeView == item.view);
        ImVec2 cpos = ImGui::GetCursorScreenPos();
        float  btnH = 42.0f;

        if (active)
        {
            dl->AddRectFilled(cpos, {cpos.x + SIDEBAR_W - 1, cpos.y + btnH},
                              IM_COL32(0, 212, 255, 18));
            dl->AddLine({cpos.x, cpos.y},
                        {cpos.x, cpos.y + btnH},
                        U32_CYAN, 3.0f);
        }

        ImGui::PushID(item.view);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32(0,212,255,15));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   IM_COL32(0,212,255,30));
        if (ImGui::Button("##btn", {SIDEBAR_W - 1, btnH}))
            g_activeView = item.view;
        ImGui::PopStyleColor(3);
        ImGui::PopID();

        ImU32 txtC = active ? U32_CYAN : U32_TEXT_MID;
        float ty   = cpos.y + (btnH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText({cpos.x + 16.0f, ty}, txtC, item.icon);
        dl->AddText({cpos.x + 36.0f, ty}, txtC, item.label);
    }

    ImGui::Dummy({1.0f, 20.0f});
    ImGui::SetCursorPosX(8.0f);

    ImVec2 dp = ImGui::GetCursorScreenPos();
    dl->AddLine(dp, {dp.x + SIDEBAR_W - 16.0f, dp.y}, U32_SEPARATOR, 1.0f);
    ImGui::Dummy({1.0f, 12.0f});

    auto sideLabel = [&](const char* lbl, const char* val, ImU32 valC)
    {
        ImGui::SetCursorPosX(12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted(lbl);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(valC));
        ImGui::TextUnformatted(val);
        ImGui::PopStyleColor();
    };

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", snap.totalReads);
    sideLabel("Leituras:", buf, U32_CYAN);

    snprintf(buf, sizeof(buf), "%d", snap.totalGranted);
    sideLabel("Liberados:", buf, U32_GREEN);

    snprintf(buf, sizeof(buf), "%d", snap.totalDenied);
    sideLabel("Negados:", buf, U32_RED);

    ImGui::Dummy({1.0f, 8.0f});

    ImVec2 bottom = {sp.x + 8.0f, sp.y + height - 30.0f};
    dl->AddText(bottom,       U32_TEXT_DIM, "GUI v1.0.0");
    dl->AddText({bottom.x, bottom.y + 14.0f}, U32_TEXT_DIM, "ImGui " IMGUI_VERSION);

    ImGui::EndChild();
}

static void DrawTerminal(const FrameSnapshot& snap, float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p       = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(p, {p.x + width, p.y + 22.0f}, U32_BG_PANEL);
    dl->AddLine({p.x, p.y + 22.0f}, {p.x + width, p.y + 22.0f},
                U32_BORDER, 1.0f);
    dl->AddText({p.x + 8.0f, p.y + 4.0f}, U32_CYAN, "▸ TERMINAL DE LOGS");

    ImGui::SetCursorScreenPos({p.x + width - 105.0f, p.y + 2.0f});
    ImGui::PushStyleColor(ImGuiCol_Button,
        g_termAutoScroll ? ImVec4{COL_CYAN.x,COL_CYAN.y,COL_CYAN.z,0.2f}
                         : ImVec4{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    if (ImGui::SmallButton(g_termAutoScroll ? "● Auto-Scroll" : "○ Auto-Scroll"))
        g_termAutoScroll = !g_termAutoScroll;
    ImGui::PopStyleColor(2);

    ImGui::SetCursorScreenPos({p.x, p.y + 22.0f});

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.04f, 0.06f, 0.08f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 6.0f});
    ImGui::BeginChild("##terminal", {width, height - 22.0f}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();

    for (const auto& [color, line] : snap.logLines)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }

    if (g_termAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawStatusBar(const FrameSnapshot& snap, float winW)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p       = ImGui::GetCursorScreenPos();

    dl->AddRectFilled({0, p.y}, {winW, p.y + STATUSBAR_H}, U32_BG_PANEL);
    dl->AddLine({0, p.y}, {winW, p.y}, U32_BORDER, 1.0f);

    float ty = p.y + (STATUSBAR_H - ImGui::GetTextLineHeight()) * 0.5f;

    int umin = static_cast<int>(snap.uptimeSeconds) / 60;
    int usec = static_cast<int>(snap.uptimeSeconds) % 60;
    char ubuf[48];
    snprintf(ubuf, sizeof(ubuf), "  ⏱  %02d:%02d  uptime", umin, usec);
    dl->AddText({8.0f, ty}, U32_TEXT_DIM, ubuf);

    char tbuf[48];
    snprintf(tbuf, sizeof(tbuf), "  ◈  %d leituras", snap.totalReads);
    dl->AddText({200.0f, ty}, U32_TEXT_DIM, tbuf);

    const char* connStr = snap.connStatus == ConnectionStatus::Connected  ? "● CONECTADO"
                        : snap.connStatus == ConnectionStatus::Connecting ? "◌ CONECTANDO"
                        : snap.connStatus == ConnectionStatus::Error       ? "✗ ERRO"
                        :                                                    "○ DESCONECTADO";
    ImU32 connC = snap.connStatus == ConnectionStatus::Connected  ? U32_GREEN
                : snap.connStatus == ConnectionStatus::Connecting ? U32_AMBER
                : snap.connStatus == ConnectionStatus::Error       ? U32_RED
                : U32_TEXT_DIM;
    ImVec2 csz = ImGui::CalcTextSize(connStr);
    dl->AddText({winW - csz.x - 12.0f, ty}, connC, connStr);

    ImGui::Dummy({winW, STATUSBAR_H});
}

int main()
{
    glfwSetErrorCallback(GLFWErrorCallback);
    if (!glfwInit())
    {
        fprintf(stderr, "Falha ao inicializar GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1280, 760,
                                          "RFID Access Control — STM32F103 GUI",
                                          nullptr, nullptr);
    if (!window)
    {
        fprintf(stderr, "Falha ao criar janela GLFW\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    bool fontLoaded = false;
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/hack/Hack-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    };
    for (const char* fp : fontPaths)
    {
        if (FILE* f = fopen(fp, "r")) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(fp, 14.0f);
            fontLoaded = true;
            printf("[GUI] Fonte carregada: %s\n", fp);
            break;
        }
    }
    if (!fontLoaded)
    {
        io.Fonts->AddFontDefault();
        printf("[GUI] Usando fonte interna do ImGui (instale fonts-hack-ttf para melhor visual)\n");
    }

    ApplyRFIDTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    g_sim = new RFIDSimulator(g_state);
    g_sim->Start();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        int winW, winH;
        glfwGetFramebufferSize(window, &winW, &winH);

        FrameSnapshot snap = SnapshotState(g_state);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({static_cast<float>(winW),
                                  static_cast<float>(winH)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoTitleBar               |
                     ImGuiWindowFlags_NoResize                 |
                     ImGuiWindowFlags_NoMove                   |
                     ImGuiWindowFlags_NoScrollbar              |
                     ImGuiWindowFlags_NoScrollWithMouse        |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar(2);

        float fw = static_cast<float>(winW);
        float fh = static_cast<float>(winH);

        ImGui::SetNextWindowPos({0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PADDING, PADDING});
        DrawTopBar(snap, fw);
        ImGui::PopStyleVar();

        float bodyY = TOPBAR_H;
        float bodyH = fh - bodyY - STATUSBAR_H;
        float termH = 200.0f;
        float contentH = bodyH - termH;

        ImGui::SetCursorPos({0, bodyY});
        DrawSidebar(snap, bodyH);

        float contentX = SIDEBAR_W;
        float contentW = fw - SIDEBAR_W;

        ImGui::SetCursorPos({contentX, bodyY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PADDING, PADDING});
        ImGui::BeginChild("##content", {contentW, contentH}, false,
                          ImGuiWindowFlags_NoScrollbar);

        switch (g_activeView)
        {
        case 0: DrawMonitorView(snap);       break;
        case 1: DrawConfigView(g_state);     break;
        case 2: DrawHistoryView(snap);       break;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SetCursorPos({contentX, bodyY + contentH});
        DrawTerminal(snap, contentW, termH);

        ImGui::SetCursorPos({0, fh - STATUSBAR_H});
        DrawStatusBar(snap, fw);

        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, winW, winH);
        glClearColor(0.047f, 0.067f, 0.090f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    delete g_sim;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
