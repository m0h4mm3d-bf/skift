#include <libgraphic/Painter.h>
#include <libmath/MinMax.h>
#include <libsystem/assert.h>
#include <libsystem/io/Stream.h>
#include <libsystem/logger.h>
#include <libwidget/Event.h>
#include <libwidget/Theme.h>
#include <libwidget/Widget.h>
#include <libwidget/Window.h>

static Font *_widget_font = NULL;
Font *widget_font(void)
{
    if (_widget_font == NULL)
    {
        _widget_font = font_create("sans");
    }

    return _widget_font;
}

void widget_initialize(
    Widget *widget,
    const char *classname,
    Widget *parent)
{
    widget->enabled = true;
    widget->classname = classname;
    widget->childs = list_create();
    widget->bound = RECTANGLE_SIZE(32, 32);

    if (parent != NULL)
    {
        widget->window = parent->window;
        widget_add_child(parent, widget);
    }
}

void widget_destroy(Widget *widget)
{
    if (widget->destroy)
    {
        widget->destroy(widget);
    }

    Widget *child = NULL;
    while (list_peek(widget->childs, (void **)&child))
    {
        widget_remove_child(widget, child);
        widget_destroy(child);
    }

    list_destroy(widget->childs);

    free(widget);
}

void widget_invalidate_layout(Widget *widget)
{
    if (widget->window)
    {
        window_schedule_layout(widget->window);
    }
}

void widget_add_child(Widget *widget, Widget *child)
{
    assert(child->parent == NULL);

    child->parent = widget;
    child->window = widget->window;
    list_pushback(widget->childs, child);

    widget_invalidate_layout(widget);
}

void widget_remove_child(Widget *widget, Widget *child)
{
    assert(child->parent == widget);

    child->parent = NULL;
    child->window = NULL;
    list_remove(widget->childs, child);

    widget_invalidate_layout(widget);
}

void widget_dump(Widget *widget, int depth)
{
    for (int i = 0; i < depth; i++)
    {
        printf("\t");
    }

    if (widget == NULL)
    {
        printf("<null>\n");
        return;
    }

    printf("%s(0x%08x) (%d, %d) %dx%d\n",
           widget->classname,
           widget,
           widget->bound.x,
           widget->bound.y,
           widget->bound.width,
           widget->bound.height);

    list_foreach(Widget, child, widget->childs)
    {
        widget_dump(child, depth + 1);
    }
}

void widget_dispatch_event(Widget *widget, Event *event)
{
    if (widget->event)
    {
        widget->event(widget, event);
    }

    if (!event->accepted && widget->event_handles[event->type].callback != NULL)
    {
        event->accepted = true;
        widget->event_handles[event->type].callback(widget->event_handles[event->type].target, widget, event);
    }

    if (!event->accepted && widget->parent)
    {
        widget_dispatch_event(widget->parent, event);
    }
}

void widget_paint(Widget *widget, Painter *painter, Rectangle rectangle)
{
    painter_push_clip(painter, widget_bound(widget));

    if (widget->paint)
    {
        widget->paint(widget, painter, rectangle);
    }

    list_foreach(Widget, child, widget->childs)
    {
        if (rectangle_colide(rectangle, child->bound))
        {
            widget_paint(child, painter, rectangle);
        }
    }

    painter_pop_clip(painter);
}

void widget_layout(Widget *widget)
{
    if (list_count(widget->childs) == 0)
        return;

    if (widget->do_layout)
    {
        widget->do_layout(widget);
        return;
    }

    Layout layout = widget->layout;

    switch (layout.type)
    {
    case LAYOUT_STACK:
        list_foreach(Widget, child, widget->childs)
        {
            child->bound = widget_content_bound(widget);
        }
        break;
    case LAYOUT_GRID:
    {
        int originX = widget_content_bound(widget).x;
        int originY = widget_content_bound(widget).y;

        int child_width = (widget_content_bound(widget).width - (layout.hspacing * (layout.hcell - 1))) / layout.hcell;
        int child_height = (widget_content_bound(widget).height - (layout.vspacing * (layout.vcell - 1))) / layout.vcell;

        int index = 0;
        list_foreach(Widget, child, widget->childs)
        {
            int x = index % layout.hcell;
            int y = index / layout.hcell;

            child->bound = RECTANGLE(
                originX + x * (child_width + layout.hspacing),
                originY + y * (child_height + layout.vspacing),
                child_width,
                child_height);

            index++;
        }
    }
    break;

    case LAYOUT_HGRID:
    {
        int current = widget_content_bound(widget).x;
        int available_space_without_spacing = widget_content_bound(widget).width - (layout.hspacing * (list_count(widget->childs) - 1));
        int child_width = available_space_without_spacing / list_count(widget->childs);
        int used_space_with_spacing = child_width * list_count(widget->childs) + (layout.hspacing * (list_count(widget->childs) - 1));
        int correction_space = widget_content_bound(widget).width - used_space_with_spacing;

        list_foreach(Widget, child, widget->childs)
        {
            if (correction_space > 0)
            {
                child->bound = RECTANGLE(current, widget_content_bound(widget).position.y, MAX(1, child_width + 1), widget_content_bound(widget).height);
                current += MAX(1, child_width + 1) + layout.hspacing;

                correction_space--;
            }
            else
            {
                child->bound = RECTANGLE(current, widget_content_bound(widget).position.y, MAX(1, child_width), widget_content_bound(widget).height);
                current += MAX(1, child_width) + layout.hspacing;
            }
        }
    }
    break;

    case LAYOUT_VGRID:
    {
        int current = widget_content_bound(widget).y;
        int available_space_without_spacing = widget_content_bound(widget).height - (layout.vspacing * (list_count(widget->childs) - 1));
        int child_height = available_space_without_spacing / list_count(widget->childs);
        int used_space_with_spacing = child_height * list_count(widget->childs) + (layout.vspacing * (list_count(widget->childs) - 1));
        int correction_space = widget_content_bound(widget).height - used_space_with_spacing;

        list_foreach(Widget, child, widget->childs)
        {
            if (correction_space > 0)
            {
                child->bound = RECTANGLE(widget_content_bound(widget).position.x, current, widget_content_bound(widget).width, MAX(1, child_height + 1));
                current += MAX(1, child_height + 1) + layout.vspacing;

                correction_space--;
            }
            else
            {
                child->bound = RECTANGLE(widget_content_bound(widget).position.x, current, widget_content_bound(widget).width, MAX(1, child_height));
                current += MAX(1, child_height) + layout.vspacing;
            }
        }
    }
    break;

    case LAYOUT_HFLOW:
    {
        int fixed_child_count = 0;
        int fixed_child_total_width = 0;

        int fill_child_count = 0;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                fill_child_count++;
            }
            else
            {
                fixed_child_count++;
                fixed_child_total_width += widget_compute_size(child).x;
            }
        }

        int usable_space =
            widget_content_bound(widget).width -
            layout.hspacing * (list_count(widget->childs) - 1);

        int fill_child_total_width = MAX(0, usable_space - fixed_child_total_width);

        int fill_child_width = (fill_child_total_width) / MAX(1, fill_child_count);

        int current = widget_content_bound(widget).x;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                child->bound = RECTANGLE(
                    current,
                    widget_content_bound(widget).position.y,
                    fill_child_width,
                    widget_content_bound(widget).height);

                current += fill_child_width + layout.hspacing;
            }
            else
            {
                child->bound = RECTANGLE(
                    current,
                    widget_content_bound(widget).position.y,
                    widget_compute_size(child).x,
                    widget_content_bound(widget).height);

                current += widget_compute_size(child).x + layout.hspacing;
            }
        }
    }
    break;

    case LAYOUT_VFLOW:
    {
        int fixed_child_count = 0;
        int fixed_child_total_height = 0;

        int fill_child_count = 0;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                fill_child_count++;
            }
            else
            {
                fixed_child_count++;
                fixed_child_total_height += widget_compute_size(child).y;
            }
        }

        int usable_space =
            widget_content_bound(widget).height -
            layout.vspacing * (list_count(widget->childs) - 1);

        int fill_child_total_height = MAX(0, usable_space - fixed_child_total_height);

        int fill_child_height = (fill_child_total_height) / MAX(1, fill_child_count);

        int current = widget_content_bound(widget).y;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                child->bound = RECTANGLE(
                    widget_content_bound(widget).position.x,
                    current,
                    widget_content_bound(widget).width,
                    fill_child_height);

                current += fill_child_height + layout.vspacing;
            }
            else
            {
                child->bound = RECTANGLE(
                    widget_content_bound(widget).position.x,
                    current,
                    widget_content_bound(widget).width,
                    widget_compute_size(child).y);

                current += widget_compute_size(child).y + layout.vspacing;
            }
        }
    }
    break;

    default:
        break;
    }

    list_foreach(Widget, child, widget->childs)
    {
        widget_layout(child);
    }
}

void widget_focus(Widget *widget)
{
    if (widget->window)
    {
        window_set_focused_widget(widget->window, widget);
    }
}

Vec2i widget_compute_size(Widget *widget)
{
    if (widget->size)
    {
        return widget->size(widget);
    }
    else
    {

        int width = widget->bound.width;
        int height = widget->bound.height;

        list_foreach(Widget, child, widget->childs)
        {
            Vec2i child_size = widget_compute_size(child);

            width = MAX(width, child_size.x);
            height = MAX(height, child_size.y);
        }

        return vec2i(width, height);
    }
}

void widget_update(Widget *widget)
{
    if (widget->window)
    {
        window_schedule_update(widget->window, widget->bound);
    }
}

void widget_update_region(Widget *widget, Rectangle bound)
{
    if (widget->window)
    {
        window_schedule_update(widget->window, bound);
    }
}

Rectangle __widget_bound(Widget *widget)
{
    return widget->bound;
}

Rectangle __widget_content_bound(Widget *widget)
{
    return rectangle_shrink(__widget_bound(widget), widget->insets);
}

Widget *widget_child_at(Widget *parent, Vec2i position)
{
    list_foreach(Widget, child, parent->childs)
    {
        if (rectangle_containe_point(widget_bound(child), position))
        {
            return widget_child_at(child, position);
        }
    }

    return parent;
}

void widget_set_event_handler(Widget *widget, EventType event, void *target, WidgetEventHandlerCallback callback)
{
    assert(event < __EVENT_TYPE_COUNT);

    widget->event_handles[event].target = target;
    widget->event_handles[event].callback = callback;
}

void widget_clear_event_handler(Widget *widget, EventType event)
{
    assert(event < __EVENT_TYPE_COUNT);

    widget->event_handles[event].target = NULL;
    widget->event_handles[event].callback = NULL;
}

Color __widget_get_color(Widget *widget, ThemeColorRole role)
{
    if (widget->color_overwrite[role].overwritten)
    {
        return widget->color_overwrite[role].color;
    }

    return window_get_color(widget->window, role);
}

void __widget_overwrite_color(Widget *widget, ThemeColorRole role, Color color)
{
    widget->color_overwrite[role].overwritten = true;
    widget->color_overwrite[role].color = color;

    widget_update(widget);
}

void widget_set_enable(Widget *widget, bool enable)
{
    if (widget->enabled != enable)
    {
        widget->enabled = enable;
        widget_update(widget);
    }
}

bool widget_is_enable(Widget *widget)
{
    return widget->enabled;
}
