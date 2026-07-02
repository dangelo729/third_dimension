// SPDX-License-Identifier: MIT

#include "TD_Scene.h"

// appends at the tail of the intrusive list, so iteration order — and thus
// the renderer's object indexing — follows add() order. an already-linked
// object is left alone (non-tail members have next_ set; a tail member of
// this scene is caught by the walk). a tail member of a different scene is
// indistinguishable from an unlinked object; one-scene-at-a-time is the
// caller's contract.
void TD_Scene::add(TD_Object &obj) {
  if (obj.next_ != nullptr) return;
  if (head_ == nullptr) {
    head_ = &obj;
    return;
  }
  TD_Object *tail = head_;
  for (;;) {
    if (tail == &obj) return;
    if (tail->next_ == nullptr) break;
    tail = tail->next_;
  }
  tail->next_ = &obj;
}

// unlinks if present (no-op otherwise) and clears next_ so the object can
// join a scene again.
void TD_Scene::remove(TD_Object &obj) {
  if (head_ == &obj) {
    head_ = obj.next_;
    obj.next_ = nullptr;
    return;
  }
  for (TD_Object *p = head_; p != nullptr; p = p->next_) {
    if (p->next_ == &obj) {
      p->next_ = obj.next_;
      obj.next_ = nullptr;
      return;
    }
  }
}

// exactly one frame: the composed renderer brackets target.beginFrame()/
// endFrame() around the pass and records the failure reason in lastStatus().
bool TD_Scene::render(TD_RenderTarget &target) {
  return renderer_.render(*this, camera, target);
}
