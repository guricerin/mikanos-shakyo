#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <numeric>
#include <vector>

#include "acpi.hpp"
#include "asmfunc.h"
#include "console.hpp"
#include "fat.hpp"
#include "font.hpp"
#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "keyboard.hpp"
#include "layer.hpp"
#include "logger.hpp"
#include "memory_manager.hpp"
#include "memory_map.hpp"
#include "message.hpp"
#include "mouse.hpp"
#include "paging.hpp"
#include "pci.hpp"
#include "segment.hpp"
#include "syscall.hpp"
#include "task.hpp"
#include "terminal.hpp"
#include "timer.hpp"
#include "usb/xhci/xhci.hpp"
#include "window.hpp"

int printk(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    char s[1024];
    int result = vsprintf(s, format, ap);
    va_end(ap);

    g_console->PutString(s);
    return result;
}

std::shared_ptr<TopLevelWindow> g_main_window;
unsigned int g_main_window_layer_id;
void InitializeMainWindow() {
    g_main_window = std::make_shared<TopLevelWindow>(160, 52, g_screen_config.pixel_format, "Hello Window");

    g_main_window_layer_id = g_layer_manager->NewLayer()
                                 .SetWindow(g_main_window)
                                 .SetDraggable(true)
                                 .Move({300, 100})
                                 .ID();
    g_layer_manager->UpDown(g_main_window_layer_id, std::numeric_limits<int>::max());
}

std::shared_ptr<TopLevelWindow> g_text_window;
unsigned int g_text_window_layer_id;
void InitializeTextWindow() {
    const int win_w = 160;
    const int win_h = 52;

    g_text_window = std::make_shared<TopLevelWindow>(win_w, win_h, g_screen_config.pixel_format, "Text Box Test");
    DrawTextbox(*g_text_window->InnerWriter(), {0, 0}, g_text_window->InnerSize());

    g_text_window_layer_id = g_layer_manager->NewLayer()
                                 .SetWindow(g_text_window)
                                 .SetDraggable(true)
                                 .Move({500, 100})
                                 .ID();

    g_layer_manager->UpDown(g_text_window_layer_id, std::numeric_limits<int>::max());
}

// ????????????????????????????????????????????????????????????
int g_text_window_index;

void DrawTextCursor(bool visible) {
    const auto color = visible ? ToColor(0) : ToColor(0xffffff);
    const auto pos = Vector2D<int>{4 + 8 * g_text_window_index, 5};
    FillRectangle(*g_text_window->InnerWriter(), pos, {7, 15}, color);
}

void InputTextWindow(char input) {
    if (input == 0) {
        return;
    }

    auto pos = []() { return Vector2D<int>{4 + 8 * g_text_window_index, 6}; };

    const int max_chars = (g_text_window->InnerSize().x - 8) / 8 - 1;
    // ??????????????????????????????????????????
    if (input == '\b' && g_text_window_index > 0) {
        DrawTextCursor(false);
        g_text_window_index--;
        FillRectangle(*g_text_window->InnerWriter(), pos(), {8, 16}, ToColor(0xffffff));
        DrawTextCursor(true);
    } else if (input >= ' ' && g_text_window_index < max_chars) {
        DrawTextCursor(false);
        WriteAscii(*g_text_window->InnerWriter(), pos(), input, ToColor(0));
        g_text_window_index++;
        DrawTextCursor(true);
    }

    g_layer_manager->Draw(g_text_window_layer_id);
}

alignas(16) uint8_t g_kernel_main_stack[1024 * 1024];

// ?????????????????????????????????????????????????????????????????????????????????????????????
extern "C" void KernelMainNewStack(const FrameBufferConfig& frame_buffer_config,
                                   const MemoryMap& memory_map,
                                   const acpi::RSDP& acpi_table,
                                   void* volume_image) {
    // ????????????????????????
    InitializeGraphics(frame_buffer_config);
    // ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    InitializeConsole();

    printk("Welcome to MikanOS!\n");
    SetLogLevel(kWarn);

    // ???????????????
    InitializeSegmentation();
    InitializePaging();
    InitializeMemoryManager(memory_map);
    InitializeTSS();
    // ????????????
    InitializeInterrupt();

    // FAT????????????????????????
    fat::Initialize(volume_image);

    // ????????????
    InitializeFont();

    // ????????????
    InitializePCI();

    // GUI????????????
    InitializeLayer();
    InitializeMainWindow();
    InitializeTextWindow();
    // ????????????????????????????????????????????????
    g_layer_manager->Draw({{0, 0}, ScreenSize()});

    // ?????????
    acpi::Initialize(acpi_table);
    InitializeLAPICTimer();

    // ?????????????????????????????????????????????
    const int kTextboxCursorTimer = 1;
    const int kTimer05sec = static_cast<int>(kTimerFreq * 0.5);
    // 0,5sec????????????????????????????????????
    g_timer_manager->AddTimer(Timer{kTimer05sec, kTextboxCursorTimer, kMainTaskID});
    bool textbox_cursor_visible = false;

    // ?????????????????????
    InitializeSyscall();

    // ??????????????????
    InitializeTask();
    // ??????????????????KernelMainStack()???
    Task& main_task = g_task_manager->CurrentTask();

    // USB????????????
    // xHCI??????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    usb::xhci::Initialize();
    InitializeKeyboard();
    InitializeMouse();

    // ????????????????????????????????????????????????
    g_app_loads = new std::map<fat::DirectoryEntry*, AppLoadInfo>;
    // ???????????????
    g_task_manager->NewTask()
        .InitContext(TaskTerminal, 0)
        .Wakeup();

    char str[128];
    // ?????????????????????????????????
    while (true) {
        // clear interrupt : ????????????????????????
        // ???????????????????????????????????????????????????????????????????????????????????????
        __asm__("cli");
        const auto tick = g_timer_manager->CurrentTick();
        // set interrupt : ????????????????????????
        __asm__("sti");
        // ?????????????????????????????????????????????????????????????????????

        sprintf(str, "%010lu", tick);
        FillRectangle(*g_main_window->InnerWriter(), {20, 4}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
        WriteString(*g_main_window->InnerWriter(), {20, 4}, str, {0, 0, 0});
        // ??????????????????????????????????????????????????????????????????
        g_layer_manager->Draw(g_main_window_layer_id);

        __asm__("cli");
        auto msg = main_task.ReceiveMessage();
        if (!msg) {
            // ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
            main_task.Sleep();
            __asm__("sti");
            continue;
        }
        __asm__("sti");

        switch (msg->type) {
        case Message::kInterruptXHCI:
            usb::xhci::ProcessEvents();
            break;
        case Message::kTimerTimeout:
            // ????????????????????????????????????????????????????????????
            if (msg->arg.timer.value == kTextboxCursorTimer) {
                __asm__("cli");
                g_timer_manager->AddTimer(Timer{msg->arg.timer.timeout + kTimer05sec, kTextboxCursorTimer, kMainTaskID});
                __asm__("sti");
                textbox_cursor_visible = !textbox_cursor_visible;
                DrawTextCursor(textbox_cursor_visible);
                g_layer_manager->Draw(g_text_window_layer_id);
            }
            break;
        case Message::kKeyPush:
            if (auto act = g_active_layer->GetActive(); act == g_text_window_layer_id) {
                // ??????????????????????????????
                if (msg->arg.keyboard.press) {
                    InputTextWindow(msg->arg.keyboard.ascii);
                }
            } else if (msg->arg.keyboard.press &&
                       msg->arg.keyboard.keycode == 59 /* F2 */) {
                g_task_manager->NewTask()
                    .InitContext(TaskTerminal, 0)
                    .Wakeup();
            } else {
                // ???????????????????????????ID????????????????????????????????????????????????????????????????????????
                __asm__("cli");
                auto task_it = g_layer_task_map->find(act);
                __asm__("sti");
                if (task_it != g_layer_task_map->end()) {
                    __asm__("cli");
                    g_task_manager->SendMessage(task_it->second, *msg);
                    __asm__("sti");
                } else {
                    printk("key push no handled: keycode %02x, ascii %02x\n",
                           msg->arg.keyboard.keycode,
                           msg->arg.keyboard.ascii);
                }
            }
            break;
        case Message::kLayer:
            // ?????????????????????????????????????????????
            // ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
            ProcessLayerMessage(*msg);
            __asm__("cli");
            // ??????????????????????????????????????????
            g_task_manager->SendMessage(msg->src_task, Message{Message::kLayerFinish});
            __asm__("sti");
            break;
        default:
            Log(kError, "Unknown message type: %d\n", msg->type);
        }
    }
}

extern "C" void __cxa_pure_virtual() {
    while (1) __asm__("hlt");
}
