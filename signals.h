#pragma once

#include <functional>
#include "intrusive_list.h"

namespace signals {

template<typename T>
struct signal;

template<typename... Args>
struct signal<void(Args...)> {
  using slot_t = std::function<void (Args...)>;

  struct connection : intrusive::list_element<struct connection_tag> {
    connection() = default;

    connection(connection &&other) : sig(other.sig), slot(other.slot) {
      safe_move(other);
    }

    connection &operator=(connection &&other) {
      if (this == &other) {
        return *this;
      }

      disconnect();
      sig = other.sig;
      slot = std::move(other.slot);
      safe_move(other);

      return *this;
    }

    void disconnect() {
      if (is_linked()) {
        unlink();
        slot = {};

        for (iteration_token *tok = sig->top_token; tok != nullptr; tok = tok->next) {
          if (tok->current != sig->connections.end() && &*tok->current == this) {
            ++tok->current;
          }
        }
      }
    }

    ~connection() {
      disconnect();
    }

   private:
    connection(signal *sig, slot_t slot) noexcept : sig(sig), slot(std::move(slot)) {
      sig->connections.push_front(*this);
    }

    void safe_move(connection &other) {
      if (other.is_linked()) {
        other.sig->connections.insert(sig->connections.as_iterator(other), *this);
        other.unlink();
        for (iteration_token *tok = sig->top_token; tok != nullptr; tok = tok->next) {
          if (tok->current != sig->connections.end() && &*tok->current == &other) {
            tok->current = sig->connections.as_iterator(*this);
          }
        }
      }
    }

    friend signal<void(Args...)>;

    signal *sig;
    slot_t slot;
  };

  using connection_t = intrusive::list<connection, struct connection_tag>;

  signal() noexcept = default;

  signal(signal const &) = delete;
  signal &operator=(signal const &) = delete;

  ~signal() {
    for (iteration_token *tok = top_token; tok != nullptr; tok = tok->next) {
      if (tok->current != connections.end()) {
        tok->destroyed = true;
      }
    }
  }

  connection connect(std::function<void(Args...)> slot) noexcept {
    return connection(this, std::move(slot));
  }

  void operator()(Args... args) const {
    iteration_token tok(this);

    try {
      while (tok.current != connections.end()) {
        auto copy = tok.current;
        tok.current++;
        copy->slot(args...);

        if (tok.destroyed) {
          return;
        }
      }
    } catch (...) {
      top_token = tok.next;
      throw;
    }
    top_token = tok.next;
  }

 private:
  struct iteration_token {
    iteration_token() noexcept = default;

    explicit iteration_token(signal const * sig) : current(sig->connections.begin()), next(sig->top_token) {
      sig->top_token = this;
    }

    typename connection_t::const_iterator current;
    iteration_token *next;
    bool destroyed = false;
  };

  connection_t connections;
  mutable iteration_token *top_token = nullptr;
};
}