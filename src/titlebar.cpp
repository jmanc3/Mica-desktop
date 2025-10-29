#include "titlebar.h"

#include "second.h"
#include "hypriso.h"
#include "events.h"
#include "drag.h"
#include "icons.h"

#include <assert.h>
#include <unistd.h>
#include <math.h>

float titlebar_button_ratio() { return hypriso->get_varfloat("plugin:mylardesktop:titlebar_button_ratio", 1.4375f); };
float titlebar_text_h() { return hypriso->get_varfloat("plugin:mylardesktop:titlebar_text_h", 15); }
float titlebar_icon_h() { return hypriso->get_varfloat("plugin:mylardesktop:titlebar_icon_h", 21); }
float titlebar_button_icon_h() { return hypriso->get_varfloat("plugin:mylardesktop:titlebar_button_icon_h", 13); }

RGBA color_titlebar_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_color", RGBA("ffffffff")); }
RGBA color_titlebar_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_color", RGBA("f0f0f0ff")); }
RGBA color_titlebar_text_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_text_color", RGBA("000000ff")); }
RGBA color_titlebar_text_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_text_color", RGBA("303030ff")); }
RGBA titlebar_button_bg_hovered_color() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_button_bg_hovered_color", RGBA("rgba(ff0000ff)")); }
RGBA titlebar_button_bg_pressed_color() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_button_bg_pressed_color", RGBA("rgba(0000ffff)")); }
RGBA titlebar_closed_button_bg_hovered_color() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_closed_button_bg_hovered_color", RGBA("rgba(ff0000ff)")); }
RGBA titlebar_closed_button_bg_pressed_color() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_closed_button_bg_pressed_color", RGBA("rgba(0000ffff)")); }
RGBA titlebar_closed_button_icon_color_hovered_pressed() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_closed_button_icon_color_hovered_pressed", RGBA("rgba(999999ff)")); }

void request_damage(Container *root, Container *c) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto b = c->real_bounds;
    b.scale(1.0 / s);
    b.grow(2.0 * s);
    hypriso->damage_box(b);
}

void titlebar_pre_layout(Container* root, Container* self, const Bounds& bounds) {
    auto rid = *datum<int>(root, "cid");
    auto cid = *datum<int>(self, "cid");
    auto s = scale(rid);
    self->wanted_bounds.h = titlebar_h * s; 
    self->children[1]->wanted_bounds.w = titlebar_h * s * titlebar_button_ratio();
    self->children[2]->wanted_bounds.w = titlebar_h * s * titlebar_button_ratio();
    self->children[3]->wanted_bounds.w = titlebar_h * s * titlebar_button_ratio();
}

TextureInfo *get_cached_texture(Container *root, Container *target, std::string needle, std::string font, std::string text, RGBA color, int wanted_h) {
    auto [rid, s, stage, active_id] = from_root(root);
    TextureInfo *info = datum<TextureInfo>(target, needle);
    
    int h = std::round(wanted_h * s);
    if (info->id != -1) { // regenerate if any of the following changes
        if (info->cached_color != color || info->cached_h != h || info->cached_text != text) {
            free_text_texture(info->id);
            info->id = -1;
            info->reattempts_count = 0;
        }
    }
    
    if (info->id == -1) {
        if (info->reattempts_count < 10) {
            info->last_reattempt_time = get_current_time_in_ms();
            info->reattempts_count++;

            auto texture = gen_text_texture(font, text, h, color);
            if (texture.id != -1) {
                info->id = texture.id;
                info->w = texture.w;
                info->h = texture.h;
                info->cached_color = color;
                info->cached_h = h;
                info->cached_text = text;
                info->reattempts_count = 0;
            }
        }
    }

    return info; 
}

void paint_button(Container *root, Container *c, std::string name, std::string icon, bool is_close = false) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto client = first_above_of(c, TYPE::CLIENT);
    assert(client);
    auto cid = *datum<int>(client, "cid");

    auto b = c->real_bounds;
    if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
        auto focused = get_cached_texture(root, root, name + "_focused", "Segoe Fluent Icons",
            icon, color_titlebar_text_focused(), titlebar_button_icon_h());
        auto unfocused = get_cached_texture(root, root, name + "_unfocused", "Segoe Fluent Icons", 
            icon, color_titlebar_text_unfocused(), titlebar_button_icon_h());
        auto closed = get_cached_texture(root, root, name + "_close_invariant", "Segoe Fluent Icons", 
            icon, titlebar_closed_button_icon_color_hovered_pressed(), titlebar_button_icon_h());

        int mask = 16;
        if (is_close)
            mask = 13;
        if (c->state.mouse_pressing) {
            auto pressed_color = is_close ? titlebar_closed_button_bg_pressed_color() : titlebar_button_bg_pressed_color();
            rect(b, pressed_color, mask, hypriso->get_rounding(cid), 2.0f);
        } else if (c->state.mouse_hovering) {
            auto hover_color = is_close ? titlebar_closed_button_bg_hovered_color() : titlebar_button_bg_hovered_color();
            rect(b, hover_color, mask, hypriso->get_rounding(cid), 2.0f);
        }

        auto texture_info = focused;
        if (!hypriso->has_focus(cid))
            texture_info = unfocused;
        if (is_close && c->state.mouse_pressing || c->state.mouse_hovering)
            texture_info = closed;
        if (texture_info->id != -1) {
            draw_texture(*texture_info, center_x(c, texture_info->w), center_y(c, texture_info->h), 1.0);
        }
    }
}

void paint_titlebar(Container *root, Container *c) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto client = first_above_of(c, TYPE::CLIENT);
    auto cid = *datum<int>(client, "cid");
    if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
        int icon_width = 0; 
        { // load icon
            TextureInfo *info = datum<TextureInfo>(client, "icon");
            if (info->id != -1) {
                if (info->cached_h != std::round(titlebar_icon_h() * s)) {
                    free_text_texture(info->id);
                    info->id = -1;
                    info->reattempts_count = 0;
                    info->last_reattempt_time = 0;
                }
            }
            if (info->id == -1 && info->reattempts_count < 30) {
                if (icons_loaded && enough_time_since_last_check(1000, info->last_reattempt_time)) {
                    info->last_reattempt_time = get_current_time_in_ms();
                    auto name = hypriso->class_name(cid);
                    auto real_icon_h = std::round(titlebar_icon_h() * s);
                    auto path = one_shot_icon(real_icon_h, {
                        name, c3ic_fix_wm_class(name), to_lower(name), to_lower(c3ic_fix_wm_class(name))
                    });
                    if (!path.empty()) {
                        *info = gen_texture(path, titlebar_icon_h() * s);
                        info->cached_h = real_icon_h;
                    }
                }
            }
            if (info->id != -1)
                icon_width = info->w;

            draw_texture(*info, c->real_bounds.x + 8 * s, center_y(c, info->h), 1.0);
        }
        
        std::string title_text = hypriso->title_name(cid);
        if (!title_text.empty()) {
            auto focused = get_cached_texture(root, client, "title_focused", "Segoe UI Variable", 
                title_text, color_titlebar_text_focused(), titlebar_text_h());
            auto unfocused = get_cached_texture(root, client, "title_unfocused", "Segoe UI Variable", 
                title_text, color_titlebar_text_unfocused(), titlebar_text_h());

            auto texture_info = focused;
            if (!hypriso->has_focus(cid))
                texture_info = unfocused;
            
            if (texture_info->id != -1) {
                auto overflow = std::max((c->real_bounds.h - texture_info->h), 0.0);
                if (icon_width != 0)
                    overflow = icon_width + 16 * s;
                draw_texture(*texture_info, 
                    c->real_bounds.x + overflow, center_y(c, texture_info->h), 1.0, c->real_bounds.w - overflow);
            }
        }
    }
}

void consume_event(Container *root, Container *c) {
    root->consumed_event = true;
    request_damage(root, c);
}

void create_titlebar(Container *root, Container *parent) {
    auto titlebar_parent = parent->child(::hbox, FILL_SPACE, FILL_SPACE); // actual wanted bounds set in pre_layout
    titlebar_parent->pre_layout = titlebar_pre_layout;
    titlebar_parent->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");

        auto b = c->real_bounds;
        if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
            auto titlebar_color = color_titlebar_focused();
            if (!hypriso->has_focus(cid))
                titlebar_color = color_titlebar_unfocused();
            rect(b, titlebar_color, 12, hypriso->get_rounding(cid), 2.0f);
        }
    };
    titlebar_parent->receive_events_even_if_obstructed_by_one = true;
    titlebar_parent->when_mouse_motion = request_damage;
    titlebar_parent->when_mouse_enters_container = titlebar_parent->when_mouse_motion;
    titlebar_parent->when_mouse_leaves_container = titlebar_parent->when_mouse_motion;

    auto titlebar = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    titlebar->when_paint = paint {
        paint_titlebar(root, c);
    };
    titlebar->when_mouse_down = consume_event;
    titlebar->when_mouse_up = consume_event;
    titlebar->when_drag_start = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");
        drag::begin(cid);
        root->consumed_event = true;
        hypriso->bring_to_front(cid);
    };
    titlebar->when_drag = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");
        drag::motion(cid);
        root->consumed_event = true;
    };
    titlebar->when_drag_end = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");
        drag::end(cid);
        root->consumed_event = true;
    };
 
    titlebar_parent->alignment = ALIGN_RIGHT;
    auto min = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    min->when_paint = paint {
        paint_button(root, c, "min", "\ue921");
    };
    min->when_mouse_down = consume_event;
    min->when_mouse_up = consume_event;
    auto max = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    max->when_paint = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        assert(client);
        bool snapped = *datum<bool>(client, "snapped");
        if (snapped) {
            paint_button(root, c, "max", "\ue923");
        } else {
            paint_button(root, c, "max", "\ue922");
        }
    };
    max->when_mouse_down = consume_event;
    max->when_mouse_up = consume_event;
    max->when_clicked = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        assert(client);
        auto snapped = datum<bool>(client, "snapped");
        *snapped = !*snapped;
        int cid = *datum<int>(client, "cid");
        hypriso->bring_to_front(cid);
    };
    auto close = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    close->when_paint = paint {
        paint_button(root, c, "close", "\ue8bb", true);
    };
    close->when_clicked = paint {
        auto client = first_above_of(c, TYPE::CLIENT);
        assert(client);
        auto cid = *datum<int>(client, "cid");
        later_immediate([cid](Timer *t) { close_window(cid); });
    };
    close->when_mouse_down = consume_event;
    close->when_mouse_up = consume_event;
}

void titlebar::on_window_open(int id) {
    if (hypriso->wants_titlebar(id)) {
        hypriso->reserve_titlebar(id, titlebar_h);

        for (auto m : monitors) {
            for (auto c : m->children) {
                auto cid = *datum<int>(c, "cid");
                if (cid == id) {
                    create_titlebar(m, c);
                    break;
                }
            }
        }
    }
}

void titlebar::on_window_closed(int id) {
    
}

void titlebar::on_draw_decos(std::string name, int monitor, int id, float a) {
    for (auto m : monitors) {
       for (auto c : m->children) {
           if (c->custom_type == (int) TYPE::CLIENT) {
               auto cid = *datum<int>(c, "cid");
               if (cid == id) {
                   paint_outline(m, c);
               }
           } 
       } 
    }
}
    
void titlebar::layout_pass() {
    
}
