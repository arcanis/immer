
#pragma once

#include <immu/detail/node.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <cassert>

namespace immu {
namespace detail {
namespace dvektor {

auto fast_log2(std::size_t x)
{
    return x == 0 ? 0 : sizeof(std::size_t) * 8 - 1 - __builtin_clzl(x);
}

template <typename T>
struct ref
{
    using inner_t    = inner_node<T>;
    using leaf_t     = leaf_node<T>;
    using node_t     = node<T>;
    using node_ptr_t = node_ptr<T>;

    unsigned depth;
    std::array<node_ptr_t, 6> display;

    template <typename ...Ts>
    static auto make_node(Ts&& ...xs)
    {
        return detail::make_node<T>(std::forward<Ts>(xs)...);
    }

    const T& get_elem(std::size_t index, std::size_t xr) const
    {
        auto display_idx = fast_log2(xr) / branching_log;
        auto node        = display[display_idx].get();
        auto shift       = display_idx * branching_log;
        while (display_idx--) {
            node = node->inner() [(index >> shift) & branching_mask].get();
            shift -= branching_log;
        }
        return node->leaf() [index & branching_mask];
    }

    node_ptr_t null_slot_and_copy_inner(node_ptr_t& node, std::size_t idx)
    {
        auto& n = node->inner();
        auto x = node_ptr_t{};
        x.swap(n[idx]);
        return copy_of_inner(x);
    }

    node_ptr_t null_slot_and_copy_leaf(node_ptr_t& node, std::size_t idx)
    {
        auto& n = node->inner();
        auto x = node_ptr_t{};
        x.swap(n[idx]);
        return copy_of_leaf(x);
    }

    node_ptr_t copy_of_inner(const node_ptr_t& n)
    {
        return make_node(n->inner());
    }

    node_ptr_t copy_of_leaf(const node_ptr_t& n)
    {
        return make_node(n->leaf());
    }

    void stabilize(std::size_t index)
    {
        auto shift = branching_log;
        for (auto i = 1u; i < depth; ++i)
        {
            display[i] = copy_of_inner(display[i]);
            display[i]->inner() [(index >> shift) & branching_mask]
                = display[i - 1];
            shift += branching_log;
        }
    }

    void goto_pos_writable_from_clean(std::size_t old_index,
                                      std::size_t index,
                                      std::size_t xr)
    {
        assert(depth);
        auto d = depth - 1;
        if (d == 0) {
            display[0] = copy_of_leaf(display[0]);
        } else {
            assert(false);
            display[d] = copy_of_inner(display[d]);
            auto shift = branching_log * d;
            while (--d) {
                display[d] = null_slot_and_copy_inner(
                    display[d + 1],
                    (index >> shift) & branching_mask);
                shift -= branching_log;
            }
            display[0] = null_slot_and_copy_leaf(
                display[1],
                (index >> branching_log) & branching_mask);
        }
    }

    void goto_pos_writable_from_dirty(std::size_t old_index,
                                      std::size_t new_index,
                                      std::size_t xr)
    {
        assert(depth);
        if (xr < (1 << branching_log)) {
            display[0] = copy_of_leaf(display[0]);
        } else {
            auto display_idx = fast_log2(xr) / branching_log;
            auto shift       = branching_log;
            for (auto i = 1u; i <= display_idx; ++i) {
                display[i] = copy_of_inner(display[i]);
                display[i]->inner() [(old_index >> shift) & branching_mask]
                    = display[i - 1];
                shift += branching_log;
            }
            for (auto i = display_idx - 1; i > 0; --i) {
                display[i] = null_slot_and_copy_inner(
                    display[i + 1],
                    (new_index >> shift) & branching_mask);
                shift -= branching_log;
            }
            display[0] = null_slot_and_copy_leaf(
                display[1],
                (new_index >> branching_log) & branching_mask);
        }
    }

    void goto_fresh_pos_writable_from_clean(std::size_t old_index,
                                            std::size_t new_index,
                                            std::size_t xr)
    {
        auto display_idx = fast_log2(xr) / branching_log;
        if (display_idx > 0) {
            auto shift       = display_idx * branching_log;
            if (display_idx == depth) {
                display[display_idx] = make_node(inner_t{});
                display[display_idx]->inner()
                    [(old_index >> shift) & branching_mask] =
                    display[display_idx - 1];
                ++depth;
            }
            while (--display_idx) {
                auto node = display[display_idx + 1]->inner()
                    [(new_index >> shift) & branching_mask];
                display[display_idx] = node
                    ? std::move(node)
                    : make_node(inner_t{});

            }
            display[0] = make_node(leaf_t{});
        }
    }

    void goto_fresh_pos_writable_from_dirty(std::size_t old_index,
                                            std::size_t new_index,
                                            std::size_t xr)
    {
        stabilize(old_index);
        goto_fresh_pos_writable_from_clean(old_index, new_index, xr);
    }
};

template <typename T>
struct impl
{
    using inner_t    = inner_node<T>;
    using leaf_t     = leaf_node<T>;
    using node_t     = node<T>;
    using node_ptr_t = node_ptr<T>;
    using ref_t      = ref<T>;

    std::size_t size;
    std::size_t focus;
    bool        dirty;
    ref_t       p;

    template <typename ...Ts>
    static auto make_node(Ts&& ...xs)
    {
        return detail::make_node<T>(std::forward<Ts>(xs)...);
    }

    void goto_pos_writable(std::size_t old_index,
                           std::size_t new_index,
                           std::size_t xr)
    {
        if (dirty) {
            p.goto_pos_writable_from_dirty(old_index, new_index, xr);
        } else {
            p.goto_pos_writable_from_clean(old_index, new_index, xr);
            dirty = true;
        }
    }

    void goto_fresh_pos_writable(std::size_t old_index,
                                 std::size_t new_index,
                                 std::size_t xr)
    {
        if (dirty) {
            p.goto_fresh_pos_writable_from_dirty(old_index, new_index, xr);
        } else {
            p.goto_fresh_pos_writable_from_clean(old_index, new_index, xr);
            dirty = true;
        }
    }

    impl push_back(T value) const
    {
        if (size) {
            auto block_index = size & ~branching_mask;
            auto lo = size & branching_mask;
            if (size != block_index) {
                auto s = impl{ size + 1, block_index, dirty, p };
                s.goto_pos_writable(focus, block_index, focus ^ block_index);
                s.p.display[0]->leaf() [lo] = std::move(value);
                return s;
            } else {
                auto s = impl{ size + 1, block_index, dirty, p };
                s.goto_fresh_pos_writable(focus, block_index, focus ^ block_index);
                s.p.display[0]->leaf() [lo] = std::move(value);
                return s;
            }
        } else {
            return impl{
                1, 0, false,
                { 1, {{ make_node(leaf_t{{std::move(value)}}) }} }
            };
        }
    }

    const T& get(std::size_t index) const
    {
        return p.get_elem(index, index ^ focus);
    }

    template <typename FnT>
    impl update(std::size_t idx, FnT&& fn) const
    {
        auto s = impl{ size, idx, dirty, p };
        s.goto_pos_writable(focus, idx, focus ^ idx);
        auto& v = s.p.display[0]->leaf() [idx & branching_mask];
        v = fn(std::move(v));
        return s;
    }

    impl assoc(std::size_t idx, T value) const
    {
        return update(idx, [&] (auto&&) {
            return std::move(value);
        });
    }
};

template <typename T> const auto    empty_inner = make_node<T>(inner_node<T>{});
template <typename T> const auto    empty_leaf  = make_node<T>(leaf_node<T>{});
template <typename T> const impl<T> empty       = {
    0,
    0,
    false,
    ref<T> {1, {}}
};

template <typename T>
struct iterator : boost::iterator_facade<
    iterator<T>,
    T,
    boost::random_access_traversal_tag,
    const T&>
{
    struct end_t {};

    iterator() = default;

    iterator(const impl<T>& v)
        : v_{ &v }
        , i_{ 0 }
    {}

    iterator(const impl<T>& v, end_t)
        : v_{ &v }
        , i_{ v.size }
    {}

private:
    friend class boost::iterator_core_access;

    const impl<T>* v_;
    std::size_t    i_;

    void increment()
    {
        assert(i_ < v_->size);
        ++i_;
    }

    void decrement()
    {
        assert(i_ > 0);
        --i_;
    }

    void advance(std::ptrdiff_t n)
    {
        assert(n <= 0 || i_ + static_cast<std::size_t>(n) <= v_->size);
        assert(n >= 0 || static_cast<std::size_t>(-n) <= i_);
        i_ += n;
    }

    bool equal(const iterator& other) const
    {
        return i_ == other.i_;
    }

    std::ptrdiff_t distance_to(const iterator& other) const
    {
        return other.i_ > i_
            ?   static_cast<std::ptrdiff_t>(other.i_ - i_)
            : - static_cast<std::ptrdiff_t>(i_ - other.i_);
    }

    const T& dereference() const
    {
        return v_->get(i_);
    }
};

} /* namespace dvektor */
} /* namespace detail */
} /* namespace immu */