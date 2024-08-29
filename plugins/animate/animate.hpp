#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/option-wrapper.hpp>

#define WF_ANIMATE_HIDING_ANIMATION (1 << 0)
#define WF_ANIMATE_SHOWING_ANIMATION (1 << 1)
#define WF_ANIMATE_MAP_STATE_ANIMATION (1 << 2)
#define WF_ANIMATE_MINIMIZE_STATE_ANIMATION (1 << 3)

namespace wf
{
namespace animate
{

enum animation_type
{
    ANIMATION_TYPE_MAP      = WF_ANIMATE_SHOWING_ANIMATION | WF_ANIMATE_MAP_STATE_ANIMATION,
    ANIMATION_TYPE_UNMAP    = WF_ANIMATE_HIDING_ANIMATION | WF_ANIMATE_MAP_STATE_ANIMATION,
    ANIMATION_TYPE_MINIMIZE = WF_ANIMATE_HIDING_ANIMATION | WF_ANIMATE_MINIMIZE_STATE_ANIMATION,
    ANIMATION_TYPE_RESTORE  = WF_ANIMATE_SHOWING_ANIMATION | WF_ANIMATE_MINIMIZE_STATE_ANIMATION,
};

class animation_base
{
  public:
    virtual void init(wayfire_view view, wf::animation_description_t duration, animation_type type);
    virtual bool step(); /* return true if continue, false otherwise */
    virtual void reverse(); /* reverse the animation */
    virtual int get_direction();

    animation_base() = default;
    virtual ~animation_base();
};

}
}

#endif
