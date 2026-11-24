#pragma once

// Static model of a menu screen: a decoded .pmb file plus its images, and the
// rules for turning one page into an ordered list of things to draw. This is
// the foundation for the reimplementation of the original menu system;
// animation, input and game-code widgets come later.

#include "formats/menu/pmb.h"
#include "formats/tex1.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::menu {

// Original menus are laid out for a 640x448 NTSC frame buffer, displayed at 4:3.
constexpr float kMenuWidth = 640.0f;
constexpr float kMenuHeight = 448.0f;

struct MenuImage {
    u32 width = 0, height = 0;
    std::vector<u32> rgba; // empty if the image failed to decode
};

struct SceneOptions {
    // Per layer of the current page; missing entries use default_layer_visible().
    std::vector<int> layer_visible; // -1 default, 0 hidden, 1 shown
    int focus = -1;                 // page item drawn in its alternative (focused) state
    bool show_items = true;
    bool show_widgets = true;       // draw the widgets' own parts
};

struct DrawItem {
    const Element* element = nullptr;
    int layer = -1;      // -1 for page items
    bool widget = false;
};

class MenuScene {
public:
    // Decodes a pmb file and all of its images. Throws FormatError.
    static MenuScene load(ByteSpan pmb_data, std::string path = {});
    static MenuScene load(const Vfs& vfs, const std::string& path);

    const std::string& path() const { return path_; }
    const PmbFile& file() const { return file_; }
    // The decoded images (decoded again first if drop_pixels let them go).
    const std::vector<MenuImage>& images() const {
        ensure_pixels();
        return images_;
    }
    // Where the pmb can be read again (a menu set's scenes: MenuSet::scene). With one, drop_pixels
    // lets the decoded images' pixels go (their sizes stay) once a renderer has them on the GPU,
    // and images() decodes them again when asked. The PlayStation Vita's 256 MB heap held every
    // GT Mode page visited (MenuSet keeps them all) and ran out (std::bad_alloc: broken menus).
    void set_source(std::function<std::optional<std::vector<u8>>()> source) { source_ = std::move(source); }
    void drop_pixels() const;
    void ensure_pixels() const;
    // Content patches (mods, src/menu/flow/menu_set.h MenuPatches): edit the layout and add
    // images (decoded RGBA) for new elements to reference. Only before the scene is drawn.
    PmbFile& mutable_file() { return file_; }
    u32 add_image(MenuImage img) {
        images_.push_back(std::move(img));
        return u32(images_.size() - 1);
    }
    size_t image_failures() const { return image_failures_; }

    // Whether a layer is shown when the user has not chosen: pages with a
    // "defaultLayer" show only it; otherwise everything but "hidden".
    bool default_layer_visible(int page, int layer) const;
    bool layer_visible(int page, int layer, const SceneOptions& o) const;

    // Everything visible on `page`, sorted back to front (stable z sort).
    std::vector<DrawItem> draw_list(int page, const SceneOptions& o) const;

private:
    // Decodes the pmb's image `i` (data: the pmb file) into `out`; false (and a warning) if it fails.
    static bool decode_image(const PmbFile& file, ByteSpan data, size_t i, MenuImage& out, const std::string& path);

    std::string path_;
    PmbFile file_;
    mutable std::vector<MenuImage> images_; // (mutable: drop_pixels / ensure_pixels)
    size_t image_failures_ = 0;
    std::function<std::optional<std::vector<u8>>()> source_;
    mutable bool pixels_dropped_ = false;
};

// Menu file naming: "menu/<LANG>/<screen><n>.pmb".
extern const char* const kMenuLanguages[]; // folders under menu/, nullptr-terminated
std::vector<std::string> list_menu_files(const Vfs& vfs, const std::string& lang, std::string_view ext);

} // namespace gt3r::menu
