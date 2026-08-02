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

  bool has_capture = false;
  for (Component* component : components) {
    if (component && component->captures_input()) {
      has_capture = true;
      break;
    }
  }

  for (Component* component : components) {
    if (!component) {
      continue;
    }
    component->ui = ctx;
    if (has_capture && !component->captures_input()) {
      continue;
    }
    component->handle_messages(mouse, keyboard);
  }
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

  if (components_sorted && !layers.empty()) {
    for (const SceneLayer& bucket : layers) {
      for (Component* component : bucket.components) {
        append_component(component, frame_commands_);
      }
    }
    for (const SceneLayer& bucket : layers) {
      for (Component* component : bucket.components) {
        append_overlay(component, frame_commands_);
      }
    }
  } else {
    for (Component* component : components) {
      append_component(component, frame_commands_);
    }
    for (Component* component : components) {
      append_overlay(component, frame_commands_);
    }
  }

  ctx->renderer.draw(ctx->window.GetContext(), ctx->window.GetWidth(),
                     ctx->window.GetHeight(), frame_commands_,
                     components_sorted);
}

void Scene::handle_events() {
  for (Component* component : components) {
    if (component) {
      component->handle_events();
    }
  }
}
