
#include "events.h"
#include "container.h"
#include <linux/input-event-codes.h>

void fill_list_with_concerned(std::vector<Container*>& containers, Container* parent) {
    if (parent->type == ::newscroll) {
        auto s = (ScrollContainer*)parent;
        for (auto child : s->content->children)
            fill_list_with_concerned(containers, child);
        if (s->right)
            fill_list_with_concerned(containers, s->right);
        if (s->bottom)
            fill_list_with_concerned(containers, s->bottom);
    } else {
        for (auto child : parent->children)
            fill_list_with_concerned(containers, child);
    }

    if (parent->state.concerned && parent->exists) {
        containers.push_back(parent);
    }
}

std::vector<Container*> concerned_containers(Container* root) {
    std::vector<Container*> containers;

    fill_list_with_concerned(containers, root);

    return containers;
}

void fill_list_with_pierced(std::vector<Container*>& containers, Container* parent, int x, int y) {
    if (!parent->exists)
        return;
    if (parent->type == ::newscroll) {
        auto s = (ScrollContainer*)parent;
        for (auto child : s->content->children) {
            if (child->interactable) {
                // parent->real_bounds w and h need to be subtracted by right and bottom
                // if they exist
                auto real_bounds_copy = parent->real_bounds;
                if (s->right && s->right->exists)
                    real_bounds_copy.w -= s->right->real_bounds.w;
                if (s->bottom && s->bottom->exists)
                    real_bounds_copy.h -= s->bottom->real_bounds.h;
                if (!bounds_contains(real_bounds_copy, x, y))
                    continue;
                fill_list_with_pierced(containers, child, x, y);
            }
        }
        if (s->right && s->right->exists)
            fill_list_with_pierced(containers, s->right, x, y);
        if (s->bottom && s->bottom->exists)
            fill_list_with_pierced(containers, s->bottom, x, y);
    } else {
        for (auto child : parent->children) {
            if (child->interactable) {
                // check if parent is scrollpane and if so, check if the child is in
                // bounds before calling
                if (parent->type >= ::scrollpane && parent->type <= ::scrollpane_b_never)
                    if (!bounds_contains(parent->real_bounds, x, y))
                        continue;
                fill_list_with_pierced(containers, child, x, y);
            }
        }
    }

    if (parent->exists) {
        if (parent->handles_pierced) {
            if (parent->handles_pierced(parent, x, y))
                containers.push_back(parent);
        } else if (bounds_contains(parent->real_bounds, x, y)) {
            containers.push_back(parent);
        }
    }
}

// Should return the list of containers directly underneath the x and y with
// deepest children first in the list
std::vector<Container*> pierced_containers(Container* root, int x, int y) {
    std::vector<Container*> containers;

    fill_list_with_pierced(containers, root, x, y);

    return containers;
}

bool is_pierced(Container* c, std::vector<Container*>& pierced) {
    for (auto container : pierced) {
        if (container == c) {
            return true;
        }
    }
    return false;
}

void handle_mouse_motion(Container* root, int x, int y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Disabled because of scrolls
    if (false && root->previous_x == x && root->previous_y == y)
        return;
    root->previous_x                  = root->mouse_current_x;
    root->previous_y                  = root->mouse_current_y;
    root->mouse_current_x             = x;
    root->mouse_current_y             = y;
    std::vector<Container*> pierced   = pierced_containers(root, x, y);
    std::vector<Container*> concerned = concerned_containers(root);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // mouse_motion can be the catalyst for sending out
    // when_mouse_enters_container, when_mouse_motion,
    // when_mouse_leaves_container, when_drag_start, and when_drag

    // If container in concerned but not in pierced means
    // when_mouse_leaves_container needs to be called and mouse_hovering needs to
    // be set to false and if the container is not doing anything else like being
    // dragged or pressed then concerned can be set to false
    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];

        bool in_pierced = false;
        for (int j = 0; j < pierced.size(); j++) {
            auto p = pierced[j];
            if (p == c) {
                in_pierced = true;
                break;
            }
        }

        if (c->state.mouse_pressing || c->state.mouse_dragging) {
            if (c->state.mouse_dragging) {
                auto move_distance_x = abs(root->mouse_initial_x - root->mouse_current_x);
                auto move_distance_y = abs(root->mouse_initial_y - root->mouse_current_y);
                if (move_distance_x >= c->minimum_x_distance_to_move_before_drag_begins || move_distance_y >= c->minimum_y_distance_to_move_before_drag_begins) {
                    // handle when_drag
                    if (c->when_drag) {
                        c->when_drag(root, c);
                    }
                }
            } else if (c->state.mouse_pressing) {
                auto move_distance_x = abs(root->mouse_initial_x - root->mouse_current_x);
                auto move_distance_y = abs(root->mouse_initial_y - root->mouse_current_y);
                if (move_distance_x >= c->minimum_x_distance_to_move_before_drag_begins || move_distance_y >= c->minimum_y_distance_to_move_before_drag_begins) {
                    c->state.mouse_dragging = true;
                    if (c->when_drag_start) {
                        c->when_drag_start(root, c);
                    }
                }
            }
        } else if (in_pierced) {
            // handle when_mouse_motion
            if (c->when_mouse_motion) {
                c->when_mouse_motion(root, c);
            }
        } else {
            // handle when_mouse_leaves_container
            c->state.mouse_hovering = false;
            if (c->when_mouse_leaves_container) {
                c->when_mouse_leaves_container(root, c);
            }
            c->state.reset();
        }
    }

    bool a_concerned_container_mouse_pressed = false;
    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];

        if (c->state.mouse_pressing || c->state.mouse_dragging)
            a_concerned_container_mouse_pressed = true;
    }

    for (int i = 0; i < pierced.size(); i++) {
        auto p = pierced[i];

        if (a_concerned_container_mouse_pressed && !p->state.concerned)
            continue;

        if (i != 0 && (!p->receive_events_even_if_obstructed_by_one && !p->receive_events_even_if_obstructed)) {
            continue;
        }
        if (i != 0) {
            if (p->receive_events_even_if_obstructed) {

            } else if (p->receive_events_even_if_obstructed_by_one && p != pierced[0]->parent) {
                continue;
            }
        }

        if (p->state.concerned)
            continue;

        // handle when_mouse_enters_container
        p->state.concerned      = true;
        p->state.mouse_hovering = true;
        if (p->when_mouse_enters_container) {
            p->when_mouse_enters_container(root, p);
        }
    }
}

void set_active(Container* root, const std::vector<Container*>& active_containers, Container* c, bool state) {
    if (c->type == ::newscroll) {
        auto s = (ScrollContainer*)c;
        for (auto child : s->content->children) {
            set_active(root, active_containers, child, state);
        }

        bool will_be_activated = false;
        for (auto active_container : active_containers) {
            if (active_container == s->content) {
                will_be_activated = true;
            }
        }
        if (will_be_activated) {
            if (!s->content->active) {
                s->content->active = true;
                if (s->content->when_active_status_changed) {
                    s->content->when_active_status_changed(root, s->content);
                }
            }
        } else {
            if (s->content->active) {
                s->content->active = false;
                if (s->content->when_active_status_changed) {
                    s->content->when_active_status_changed(root, s->content);
                }
            }
        }
    } else {
        for (auto child : c->children) {
            set_active(root, active_containers, child, state);
        }

        bool will_be_activated = false;
        for (auto active_container : active_containers) {
            if (active_container == c) {
                will_be_activated = true;
            }
        }
        if (will_be_activated) {
            if (!c->active) {
                c->active = true;
                if (c->when_active_status_changed) {
                    c->when_active_status_changed(root, c);
                }
            }
        } else {
            if (c->active) {
                c->active = false;
                if (c->when_active_status_changed) {
                    c->when_active_status_changed(root, c);
                }
            }
        }
    }
}

void handle_mouse_button_press(Container* root, const Event& e) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // TODOO wrong check
    if (e.button == BTN_LEFT)
        root->left_mouse_down = true;

    root->mouse_initial_x             = e.x;
    root->mouse_initial_y             = e.y;
    root->mouse_current_x             = e.x;
    root->mouse_current_y             = e.y;
    std::vector<Container*> pierced   = pierced_containers(root, e.x, e.y);
    std::vector<Container*> concerned = concerned_containers(root);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // handle_mouse_button can be the catalyst for sending out when_mouse_down and
    // when_scrolled

    std::vector<Container*> mouse_downed;

    for (int i = 0; i < pierced.size(); i++) {
        auto p = pierced[i];

        // pierced[0] will always be the top or "real" container we are actually
        // clicking on therefore everyone else in the list needs to set
        // receive_events_even_if_obstructed to true to receive events
        if (i != 0 && (!p->receive_events_even_if_obstructed_by_one && !p->receive_events_even_if_obstructed)) {
            continue;
        }
        if (i != 0) {
            if (p->receive_events_even_if_obstructed) {

            } else if (p->receive_events_even_if_obstructed_by_one && p != pierced[0]->parent) {
                continue;
            }
        }

        p->state.concerned = true; // Make sure this container is concerned

        // Check if its a scroll event and call when_scrolled if so
        if (e.button >= 4 && e.button <= 7) {
            /*
      if (!app->attempted_to_verify_if_raw_scroll_available) {
          app->attempted_to_verify_if_raw_scroll_available = true;
          app_timeout_create(app, client, 30, [](App *app, AppClient *, Timeout
      *, void *) { app->raw_scroll_available = app->has_seen_raw_scroll;
          }, nullptr, "attempt to verify if raw scrolls are happening");
      }
      if (!app->raw_scroll_available) {
          if (p->when_fine_scrolled || app->wayland) {
              if (e.button == 4 || e.button == 5) {
                  if (app->wayland && p->when_fine_scrolled) {
                      p->when_fine_scrolled(client, client->cr, p, 0,
                                            e.button == 4 ? 1 * 12 * 3 *
      config->dpi : -1 * 12 * 3 * config->dpi, false); } else if
      (p->when_fine_scrolled) { p->when_fine_scrolled(client, client->cr, p, 0,
                                            e.button == 4 ? 1 * 12 * 3 *
      config->dpi : -1 * 12 * 3 * config->dpi, false);
                  }
              } else {
                  if (app->wayland && p->when_fine_scrolled) {
                      p->when_fine_scrolled(client, client->cr, p,
                                            e.button == 6 ? 1 * 12 * 3 *
      config->dpi : -1 * 12 * 3 * config->dpi, 0, false); } else if
      (p->when_scrolled) { p->when_fine_scrolled(client, client->cr, p, e.button
      == 6 ? 1 * 12 * 3 * config->dpi : -1 * 12 * 3 * config->dpi, 0, false);
                  }
              }
          }
          handle_mouse_motion(app, client, e.event_x, e.event_y);
      }
      continue;
      */
        }

        if (e.button != BTN_LEFT && e.button != BTN_RIGHT && e.button != BTN_MIDDLE) {
            continue;
        }

        // Update state and call when_mouse_down
        p->state.mouse_hovering = true; // If this container is pressed then clearly
                                        // we are hovered as well
        p->state.mouse_pressing       = true;
        p->state.mouse_button_pressed = e.button;

        if (p->when_mouse_down || p->when_key_event) {
            mouse_downed.push_back(p);
            if (p->when_mouse_down) {
                p->when_mouse_down(root, p);
            }
        }
    }
    set_active(root, mouse_downed, root, false);
}

bool handle_mouse_button_release(Container* root, const Event& e) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (e.button != BTN_LEFT && e.button != BTN_RIGHT && e.button != BTN_MIDDLE) {
        return true;
    }

    if (e.button == BTN_LEFT)
        root->left_mouse_down = false;

    root->mouse_current_x             = e.x;
    root->mouse_current_y             = e.y;
    std::vector<Container*> concerned = concerned_containers(root);
    std::vector<Container*> pierced   = pierced_containers(root, e.x, e.y);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // handle_mouse_button_release can be the catalyst for sending out
    // when_mouse_up, when_drag_end, and_when_clicked

    for (int i = 0; i < concerned.size(); i++) {
        auto                c           = concerned[i];
        std::weak_ptr<bool> still_alive = c->lifetime;
        bool                p           = is_pierced(c, pierced);

        if (c->when_mouse_leaves_container && !p) {
            c->when_mouse_leaves_container(root, c);
        }
        if (!still_alive.lock())
            continue;

        if (c->when_drag_end) {
            if (c->state.mouse_dragging) {
                c->when_drag_end(root, c);
            }
        }
        if (!still_alive.lock())
            continue;

        if (c->when_clicked) {
            if (c->when_drag_end_is_click && c->state.mouse_dragging && p) {
                c->when_clicked(root, c);
            } else if ((!c->state.mouse_dragging)) {
                c->when_clicked(root, c);
            }
        }
        if (!still_alive.lock())
            continue;

        // TODO: when_clicked could've delete'd 'c' so recheck for it
        c->state.mouse_pressing = false;
        c->state.mouse_dragging = false;
        c->state.mouse_hovering = p;
        c->state.concerned      = p;
        if (c->name == "asdfasdfasdf")
            c->name = "";
    }

    handle_mouse_motion(root, e.x, e.y);

    return false;
}

void move_event(Container* root, const Event& e) {
    handle_mouse_motion(root, e.x, e.y);
}

void mouse_event(Container* root, const Event& e) {
    if (e.state) {
        handle_mouse_button_press(root, e);
    } else {
        handle_mouse_button_release(root, e);
    }
}

void paint_outline(Container* root, Container* c) {
    if (c->when_paint) {
        c->when_paint(root, c);
    }
    for (auto ch : c->children) {
        paint_outline(root, ch);
    }

    if (c->after_paint) {
        c->after_paint(root, c);
    }
}

void paint_root(Container* c) {
    paint_outline(c, c);
}
