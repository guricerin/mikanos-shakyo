#include "layer.hpp"

#include <algorithm>

#include "console.hpp"
#include "logger.hpp"

Layer::Layer(unsigned int id) : id_{id} {}

unsigned int Layer::ID() const {
    return id_;
}

Layer& Layer::SetWindow(const std::shared_ptr<Window>& window) {
    window_ = window;
    return *this;
}

std::shared_ptr<Window> Layer::GetWindow() const {
    return window_;
}

Vector2D<int> Layer::GetPosition() const {
    return pos_;
}

Layer& Layer::SetDraggable(bool draggable) {
    draggable_ = draggable;
    return *this;
}

bool Layer::IsDraggable() const {
    return draggable_;
}

Layer& Layer::Move(Vector2D<int> pos) {
    pos_ = pos;
    return *this;
}

Layer& Layer::MoveRelative(Vector2D<int> pos_diff) {
    pos_ += pos_diff;
    return *this;
}

void Layer::DrawTo(FrameBuffer& screen, const Rectangle<int>& area) const {
    if (window_) {
        window_->DrawTo(screen, pos_, area);
    }
}

void LayerManager::SetScreen(FrameBuffer* screen) {
    screen_ = screen;

    // バックバッファ初期化
    FrameBufferConfig back_config = screen->Config();
    back_config.frame_buffer = nullptr;
    back_buffer_.Initailize(back_config);
}

Layer& LayerManager::NewLayer() {
    latest_id_++;
    return *layers_.emplace_back(new Layer{latest_id_});
}

void LayerManager::Draw(const Rectangle<int>& area) const {
    for (auto layer : layer_stack_) {
        layer->DrawTo(back_buffer_, area);
    }
    screen_->Copy(area.pos, back_buffer_, area);
}

void LayerManager::Draw(unsigned int id) const {
    bool draw = false;
    Rectangle<int> window_area;
    for (auto layer : layer_stack_) {
        // 再描画されたレイヤーより前面のレイヤーはすべて再描画
        if (layer->ID() == id) {
            window_area.size = layer->GetWindow()->Size();
            window_area.pos = layer->GetPosition();
            draw = true;
        }
        if (draw) {
            layer->DrawTo(back_buffer_, window_area);
        }
    }
    screen_->Copy(window_area.pos, back_buffer_, window_area);
}

void LayerManager::Move(unsigned int id, Vector2D<int> new_position) {
    auto layer = FindLayer(id);
    const auto window_size = layer->GetWindow()->Size();
    const auto old_pos = layer->GetPosition();
    layer->Move(new_position);
    Draw({old_pos, window_size});
    Draw(id);
}

void LayerManager::MoveRelative(unsigned int id, Vector2D<int> pos_diff) {
    auto layer = FindLayer(id);
    const auto window_size = layer->GetWindow()->Size();
    const auto old_pos = layer->GetPosition();
    layer->MoveRelative(pos_diff);
    Draw({old_pos, window_size});
    Draw(id);
}

void LayerManager::UpDown(unsigned int id, int new_height) {
    if (new_height < 0) {
        Hide(id);
        return;
    }

    if (new_height > layer_stack_.size()) {
        new_height = layer_stack_.size();
    }

    auto layer = FindLayer(id);
    auto old_pos = std::find(layer_stack_.begin(), layer_stack_.end(), layer);
    auto new_pos = layer_stack_.begin() + new_height;

    if (old_pos == layer_stack_.end()) {
        layer_stack_.insert(new_pos, layer);
        return;
    }

    if (new_pos == layer_stack_.end()) {
        new_pos--;
    }
    layer_stack_.erase(old_pos);
    layer_stack_.insert(new_pos, layer);
}

void LayerManager::Hide(unsigned int id) {
    auto layer = FindLayer(id);
    auto pos = std::find(layer_stack_.begin(), layer_stack_.end(), layer);
    if (pos != layer_stack_.end()) {
        layer_stack_.erase(pos);
    }
}

Layer* LayerManager::FindLayerByPosition(Vector2D<int> pos, unsigned int exclude_id) const {
    auto pred = [pos, exclude_id](Layer* layer) {
        if (layer->ID() == exclude_id) {
            return false;
        }
        const auto& win = layer->GetWindow();
        if (!win) {
            return false;
        }

        const auto win_pos = layer->GetPosition();
        const auto win_end_pos = win_pos + win->Size();
        return win_pos.x <= pos.x && pos.x < win_end_pos.x &&
               win_pos.y <= pos.y && pos.y < win_end_pos.y;
    };

    // find back
    auto it = std::find_if(layer_stack_.rbegin(), layer_stack_.rend(), pred);
    if (it == layer_stack_.rend()) {
        return nullptr;
    }
    return *it;
}
Layer* LayerManager::FindLayer(unsigned int id) {
    auto pred = [id](const std::unique_ptr<Layer>& elem) {
        return elem->ID() == id;
    };
    auto it = std::find_if(layers_.begin(), layers_.end(), pred);
    if (it == layers_.end()) {
        return nullptr;
    }
    return it->get();
}

namespace {
    /// 本物のフレームバッファー
    FrameBuffer* g_screen;
} // namespace

LayerManager* g_layer_manager;

void InitializeLayer() {
    const auto screen_size = ScreenSize();

    // 背景ウィンドウ
    auto bg_window = std::make_shared<Window>(screen_size.x, screen_size.y, g_screen_config.pixel_format);
    DrawDesktop(*bg_window->Writer());

    // コンソール
    auto console_window = std::make_shared<Window>(Console::kColumns * 8, Console::kRows * 16, g_screen_config.pixel_format);
    // レイヤーマネージャーの準備が整ったので、コンソールをレイヤーの仕組みに載せ替える
    g_console->SetWindow(console_window);

    g_screen = new FrameBuffer;
    if (auto err = g_screen->Initailize(g_screen_config)) {
        Log(kError, "failed to initialize frame buffer: %s at %s:%d\n",
            err.Name(), err.File(), err.Line());
        exit(1);
    }

    g_layer_manager = new LayerManager;
    g_layer_manager->SetScreen(g_screen);

    auto bg_layer_id = g_layer_manager->NewLayer()
                           .SetWindow(bg_window)
                           .Move({0, 0})
                           .ID();
    g_console->SetLayerID(g_layer_manager->NewLayer()
                              .SetWindow(console_window)
                              .Move({0, 0})
                              .ID());

    g_layer_manager->UpDown(bg_layer_id, 0);
    g_layer_manager->UpDown(g_console->LayerID(), 1);
}

void ProcessLayerMessage(const Message& msg) {
    const auto& arg = msg.arg.layer;
    switch (arg.op) {
    case LayerOperation::Move:
        g_layer_manager->Move(arg.layer_id, {arg.x, arg.y});
        break;
    case LayerOperation::MoveRelative:
        g_layer_manager->MoveRelative(arg.layer_id, {arg.x, arg.y});
        break;
    case LayerOperation::Draw:
        g_layer_manager->Draw(arg.layer_id);
        break;
    default:
        Log(kError, "LayerOperation is not exhausted");
        exit(1);
    }
}