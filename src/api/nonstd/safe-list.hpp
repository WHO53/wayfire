#ifndef WF_SAFE_LIST_HPP
#define WF_SAFE_LIST_HPP

#include <list>
#include <memory>
#include <algorithm>
#include <functional>

#include <wayland-server.h>

/* This is a trimmed-down wrapper of std::list<T>.
 *
 * It supports safe iteration over all elements in the collection, where any
 * element can be deleted from the list at any given time (i.e even in a
 * for-each-like loop) */
namespace wf
{
    /* The object type depends on the safe list type, and the safe list type
     * needs access to the event loop. However, the event loop is typically
     * available from core.hpp. Because we can't resolve this circular dependency,
     * we provide a link to the event loop specially for the safe list */
    namespace _safe_list_detail
    {
        /* In main.cpp, and initialized there */
        extern wl_event_loop* event_loop;
        void idle_cleanup_func(void *data);
    }

    template<class T>
    class safe_list_t
    {
        std::list<std::unique_ptr<T>> list;
        wl_event_source *idle_cleanup_source = NULL;

        /* Remove all invalidated elements in the list */
        std::function<void()> do_cleanup = [&] ()
        {
            auto it = list.begin();
            while (it != list.end())
            {
                if (*it) {
                    ++it;
                } else {
                    it = list.erase(it);
                }
            }

            idle_cleanup_source = NULL;
        };

        /* Return whether the list has invalidated elements */
        bool is_dirty() const
        {
            return idle_cleanup_source;
        }

        public:
        safe_list_t() {};

        /* Copy the not-erased elements from other, but do not copy the idle source */
        safe_list_t(const safe_list_t& other) { *this = other; }
        safe_list_t& operator = (const safe_list_t& other)
        {
            this->idle_cleanup_source = NULL;
            other.for_each([&] (auto& el) {
                this->push_back(el);
            });
        }

        safe_list_t(safe_list_t&& other) = default;
        safe_list_t& operator = (safe_list_t&& other) = default;

        ~safe_list_t()
        {
            if (idle_cleanup_source)
                wl_event_source_remove(idle_cleanup_source);
        }

        T& back()
        {
            /* No invalidated elements */
            if (!is_dirty())
                return *list.back();

            auto it = list.rbegin();
            while (it != list.rend() && (*it) == nullptr)
                ++it;

            if (it == list.rend())
                throw std::out_of_range("back() called on an empty list!");

            return **it;
        }

        size_t size() const
        {
            if (!is_dirty())
                return list.size();

            /* Count non-null elements, because that's the real size */
            size_t sz = 0;
            for (auto& it : list)
                sz += (it != nullptr);

            return sz;
        }

        /* Push back by copying */
        void push_back(T value)
        {
            list.push_back(std::make_unique<T> (std::move(value)));
        }

        /* Push back by moving */
        void emplace_back(T&& value)
        {
            list.push_back(std::make_unique<T> (value));
        }

        /* Call func for each non-erased element of the list */
        void for_each(std::function<void(T&)> func) const
        {
            for (auto& el : list)
            {
                /* The for-each loop here is safe, because no elements will be
                 * erased util the event loop goes idle */
                if (el)
                    func(*el);
            }
        }

        /* Remove all elements equal to value, by resetting their pointers
         * and scheduling a cleanup operation */
        void remove_all(const T& value)
        {
            bool actually_removed = false;
            for (auto& it : list)
            {
                if (it && *it == value)
                {
                    actually_removed = true;
                    /* First reset the element in the list, and then free resources */
                    auto copy = std::move(it);
                    it = nullptr;
                    /* Now copy goes out of scope */
                }
            }

            /* Schedule a clean-up, but be careful to not schedule it twice */
            if (!idle_cleanup_source && actually_removed)
            {
                idle_cleanup_source = wl_event_loop_add_idle(_safe_list_detail::event_loop,
                    _safe_list_detail::idle_cleanup_func, &do_cleanup);
            }
        }
    };
}

#endif /* end of include guard: WF_SAFE_LIST_HPP */
