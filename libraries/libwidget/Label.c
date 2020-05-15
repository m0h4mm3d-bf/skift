#include <libgraphic/Painter.h>
#include <libsystem/cstring.h>
#include <libwidget/Label.h>
#include <libwidget/Window.h>

void label_paint(Label *label, Painter *painter, Rectangle rectangle)
{
    __unused(rectangle);
    int text_width = font_measure_string(widget_font(), label->text);

    painter_draw_string(
        painter,
        widget_font(),
        label->text,
        vec2i(
            widget_bound(label).x + widget_bound(label).width / 2 - text_width / 2,
            widget_bound(label).y + widget_bound(label).height / 2 + 4),
        widget_get_color(label, THEME_FOREGROUND));
}

Vec2i label_size(Label *label)
{
    return vec2i(font_measure_string(widget_font(), label->text), 16);
}

void label_set_text(Widget *label, const char *text)
{
    free(((Label *)label)->text);
    ((Label *)label)->text = strdup(text);

    widget_update(label);
}

void label_destroy(Label *label)
{
    free(label->text);
}

Widget *label_create(Widget *parent, const char *text)
{
    Label *label = __create(Label);

    label->text = strdup(text);

    WIDGET(label)->paint = (WidgetPaintCallback)label_paint;
    WIDGET(label)->destroy = (WidgetDestroyCallback)label_destroy;
    WIDGET(label)->size = (WidgetComputeSizeCallback)label_size;

    widget_initialize(WIDGET(label), "Label", parent);

    return WIDGET(label);
}
