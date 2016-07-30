
#pragma once

#include <array>
#include <memory>
#include <cassert>

namespace immu {
namespace detail {

constexpr auto branching_log  = 5;
constexpr auto branching      = 1 << branching_log;
constexpr auto branching_mask = branching - 1;

template <typename T>
struct node;

template <typename T>
using node_ptr = std::shared_ptr<node<T> >;

template <typename T>
using leaf_node  = std::array<T, branching>;

template <typename T>
using inner_node = std::array<node_ptr<T>, branching>;

template <typename T>
struct node
{
    enum
    {
        leaf_kind,
        inner_kind
    } kind;

    union data_t
    {
        leaf_node<T>  leaf;
        inner_node<T> inner;
        data_t(leaf_node<T> n)  : leaf(std::move(n)) {}
        data_t(inner_node<T> n) : inner(std::move(n)) {}
        ~data_t() {}
    } data;

    ~node()
    {
        switch (kind) {
        case leaf_kind:
            data.leaf.~leaf_node<T>();
            break;
        case inner_kind:
            data.inner.~inner_node<T>();
            break;
        }
    }

    node(leaf_node<T> n)
        : kind{leaf_kind}
        , data{std::move(n)}
    {}

    node(inner_node<T> n)
        : kind{inner_kind}
        , data{std::move(n)}
    {}

    inner_node<T>& inner() & {
        assert(kind == inner_kind);
        return data.inner;
    }
    const inner_node<T>& inner() const& {
        assert(kind == inner_kind);
        return data.inner;
    }
    inner_node<T>&& inner() && {
        assert(kind == inner_kind);
        return std::move(data.inner);
    }

    leaf_node<T>& leaf() & {
        assert(kind == leaf_kind);
        return data.leaf;
    }
    const leaf_node<T>& leaf() const& {
        assert(kind == leaf_kind);
        return data.leaf;
    }
    leaf_node<T>&& leaf() && {
        assert(kind == leaf_kind);
        return std::move(data.leaf);
    }
};

template <typename T, typename ...Ts>
auto make_node(Ts&& ...xs)
{
    return std::make_shared<node<T>>(std::forward<Ts>(xs)...);
}

} /* namespace detail */
} /* namespace immu */