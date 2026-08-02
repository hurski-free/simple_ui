#include "Scene.h"

#include "../UiContext.h"

#include <algorithm>
#include <vector>

Scene::Scene() = default;

Scene::~Scene() = default;

void Scene::prepare_scene() {
  layers.clear();

  std::vector<Component*> ordered;
  ordered.reserve(components.size());
  for (Component* component : components) {
    if (component) {
      ordered.push_back(component);
    }
  }

  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const Component* a, const Component* b) {
                     return a->layer < b->layer;
                   });

  for (Component* component : ordered) {
    if (layers.empty() || layers.back().layer != component->layer) {
      SceneLayer bucket;
      bucket.layer = component->layer;
      layers.push_back(std::move(bucket));
    }
    layers.back().components.push_back(component);
  }

  components_sorted = true;
}

void Scene::handle_messages(UiContext* ctx) {
  if (!ctx) {
    return;
  }

  const MouseEvents& mouse = ctx->window.GetMouseEvents();
  const KeyboardEvents& keyboard = ctx->window.GetKeyboardEvents();

  // Topmost first (high layer, then reverse within layer).
  // Capturing widgets (open Select, focused TextArea, Modal, ...) run first so
  // they can consume clicks/wheel before siblings underneath.
  auto for_each_topmost = [&](auto&& fn) {
    if (components_sorted && !layers.empty()) {
      for (auto layer_it = layers.rbegin(); layer_it != layers.rend();
           ++layer_it) {
        const std::vector<Component*>& comps = layer_it->components;
        for (auto it = comps.rbegin(); it != comps.rend(); ++it) {
          if (*it) {
            fn(*it);
          }
        }
      }
    } else {
      for (auto it = components.rbegin(); it != components.rend(); ++it) {
        if (*it) {
          fn(*it);
        }
      }
    }
  };

  // Snapshot capturers up front: a Select may close during its own handler and
  // must not be dispatched a second time in the same frame.
  std::vector<Component*> capturers;
  for_each_topmost([&](Component* component) {
    if (component->captures_input()) {
      capturers.push_back(component);
    }
  });

  for (Component* component : capturers) {
    component->ui = ctx;
    component->handle_messages(mouse, keyboard);
  }

  for_each_topmost([&](Component* component) {
    if (std::find(capturers.begin(), capturers.end(), component) !=
        capturers.end()) {
      return;
    }
    component->ui = ctx;
    component->handle_messages(mouse, keyboard);
  });
}

void Scene::update(float dt) {
  for (Component* component : components) {
    if (component) {
      component->update(dt);
    }
  }
}

void Scene::draw(UiContext* ctx) {
  if (!ctx || !ctx->renderer.is_valid()) {
    return;
  }

  frame_commands_.clear();
  if (frame_commands_.capacity() < 256) {
    frame_commands_.reserve(256);
  }

  auto append_component = [ctx](Component* component,
                                std::vector<DrawCommand*>& out) {
    if (component) {
      component->ui = ctx;
      component->collect_draw(out);
    }
  };
  auto append_overlay = [ctx](Component* component,
                              std::vector<DrawCommand*>& out) {
    if (component) {
      component->ui = ctx;
      component->collect_overlay_draw(out);
    }
  };

  // Pass 1: main UI. Overlays are collected separately so popups (e.g. Select
  // dropdown) always composite on top regardless of component layer order.
  if (components_sorted && !layers.empty()) {
    for (const SceneLayer& bucket : layers) {
      for (Component* component : bucket.components) {
        append_component(component, frame_commands_);
      }
    }
  } else {
    for (Component* component : components) {
      append_component(component, frame_commands_);
    }
  }

  ctx->renderer.draw(ctx->window.GetContext(), ctx->window.GetWidth(),
                     ctx->window.GetHeight(), frame_commands_,
                     components_sorted);

  frame_commands_.clear();
  if (components_sorted && !layers.empty()) {
    for (const SceneLayer& bucket : layers) {
      for (Component* component : bucket.components) {
        append_overlay(component, frame_commands_);
      }
    }
  } else {
    for (Component* component : components) {
      append_overlay(component, frame_commands_);
    }
  }

  if (!frame_commands_.empty()) {
    // Preserve emit order; do not re-sort by layer (would defeat "always on top").
    ctx->renderer.draw(ctx->window.GetContext(), ctx->window.GetWidth(),
                       ctx->window.GetHeight(), frame_commands_, true);
  }
}

void Scene::handle_events() {
  for (Component* component : components) {
    if (component) {
      component->handle_events();
    }
  }
}
