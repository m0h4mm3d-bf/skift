#include <libio/Format.h>
#include <libwidget/Label.h>

#include "panel/widgets/DateAndTime.h"

namespace panel
{

DateAndTime::DateAndTime(Component *parent) : Button(parent, Button::TEXT)
{
    auto label = new Widget::Label(this, "");

    _timer = own<Async::Timer>(1000, [this, label]() {
        TimeStamp timestamp = timestamp_now();
        DateTime datetime = timestamp_to_datetime(timestamp);

        label->text(IO::format("{02d}:{02d}:{02d}", datetime.hour, datetime.minute, datetime.second));
    });

    _timer->start();
}

} // namespace panel
