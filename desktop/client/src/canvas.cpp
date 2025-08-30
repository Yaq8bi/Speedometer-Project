#include "canvas.h"
#include "setting.h"
#include <QPainter>
#include <QObject>
#include <QtMultimedia/QMediaPlayer>
#include <QAudioOutput>
#include <qcoreapplication.h>
#include <iostream>

static int battery_value = 0;     // Level in percentage, range 0-100
static int battery_text_size = 14; // Font size for the level text
static int battery_icon_size = 50; // Font size for the battery icon

static bool connection_status = false; // Connection status (true = connected, false = disconnected)
static int connection_icon_size = 50;  // Font size for the level text
static int connection_text_size = 20;  // Font size for the battery icon

static bool indicator_left = false;
static bool indicator_right = false;
static bool indicator_visible = false;
static int indicator_icon_size = 50; // Font size for the icon

static int speedometer_speed = 0;
static int speedometer_font_size = 18;

static int thermometer_value = 0;    // Temperature in Celsius, range 0-100
static int thermometer_text_size = 14; // Font size for the temperature text
static int thermometer_icon_size = 50; // Font size for the thermometer icon

static Setting::Signal &signal{Setting::Signal::handle()};
static bool audio_state = false; // Audio state for the indicator sound

void Canvas::updateIndicatorAudio() // Helper function for controlling audio playback for all indicator modes
{
    bool active = indicator_left || indicator_right;

    if (active)
    {
        if (player.playbackState() != QMediaPlayer::PlayingState)
        {
            player.setSource(QUrl()); // clear to escape dead state
            player.setSource(QUrl::fromLocalFile(
                QCoreApplication::applicationDirPath() + "/sound.wav"));
            player.setPosition(0);
            player.play();
        }
    }
    else
    {
        player.stop();
    }
}

void Canvas::battery_set_level(int val)
{
    int max = signal["battery"].max;
    int min = signal["battery"].min;
    battery_value = std::clamp(val, min, max); // If clamp isn't needed, use raw values. already have a limit at server.
}

void Canvas::battery_draw(QPainter &painter, const QPoint &pos)
{
    // Color variables
    QColor temp_colors[3] = {QColor(255, 0, 0), QColor(255, 255, 0), QColor(0, 255, 0)}; // 0 = Red, 1 = Yellow, 2 = Green

    QColor color;
    if (battery_value < 25)
    {
        color = temp_colors[0]; // Red for low level
    }
    else if (battery_value < 50)
    {
        color = temp_colors[1]; // Yellow for medium level
    }
    else
    {
        color = temp_colors[2]; // Green for high level
    }

    painter.setPen(color);

    // Box to fill the battery level
    //  Calculate the height of the filled box based on battery level (value: 0-100)
    int fill_height = static_cast<int>((battery_value / 100.0) * (battery_icon_size - battery_icon_size * 0.20));
    int rect_x = pos.x() + battery_icon_size / 2;
    int rect_y = pos.y() - fill_height - battery_icon_size * 0.22;
    QRectF battery_rect(rect_x, rect_y, battery_icon_size / 2 - (battery_icon_size * 0.1), fill_height);
    painter.setBrush(color);
    painter.drawRect(battery_rect);

    // Print the QChar battery icon
    QChar qchar(0xebdc);
    QFont font("MaterialIcons.ttf");
    font.setPointSize(battery_icon_size);
    painter.setFont(font);
    painter.drawText(pos.x(), pos.y(), QString(qchar));

    painter.setPen(QColor(255, 255, 255));
    // Draw the battery level under the icon
    QString temp_text = QString::number(battery_value, 'f', 0) + " %";
    QFont temp_font("Arial", battery_text_size);
    painter.setFont(temp_font);
    painter.drawText(pos.x() + battery_icon_size / 4, pos.y() + battery_text_size, temp_text);
}

void Canvas::indicator_set_left(int left)
{
    int max = signal["signal-left"].max;
    int min = signal["signal-left"].min;

    int indicator_left_v = std::clamp(left, min, max);
    indicator_left = indicator_left_v ? true : false; // Convert to boolean

    updateIndicatorAudio();
}

void Canvas::indicator_set_right(int right)
{
    int max = signal["signal-right"].max;
    int min = signal["signal-right"].min;

    int indicator_right_v = std::clamp(right, min, max);
    indicator_right = indicator_right_v ? true : false; // Convert to boolean

    updateIndicatorAudio();
}

void Canvas::indicator_set_visable(bool _visible)
{
    indicator_visible = _visible;
}

void Canvas::indicator_draw(QPainter &painter, const QPoint &position, const int distance)
{
    if (indicator_visible == false)
    {
        return; // Do not draw if not visible
    }

    // Set the icon based on the left/right state
    QChar char_left = QChar(0xe5c4);
    QChar char_right = QChar(0xe5c8); // Left or right arrow icon

    QFont temp_font("MaterialIcons.ttf");
    temp_font.setPointSize(indicator_icon_size);
    painter.setFont(temp_font);

    painter.setPen(QColor(0, 255, 0));

    if (indicator_left)
    {
        painter.drawText(position.x(), position.y(), QString(char_left));
    }
    if (indicator_right)
    {
        painter.drawText(position.x() + distance, position.y(), QString(char_right));
    }
}

void Canvas::needle_draw(QPainter &painter, const QPoint &pos, const double speed)
{
    // Draw circle for the needle
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 0, 0));          // Red color for the needle
    painter.drawEllipse(pos, 10, 10);             // Draw a circle at the center
    painter.setPen(QPen(Qt::red, 5));             // Set pen for the needle line
    painter.setBrush(Qt::NoBrush);                // No fill for the needle line
    double angle = 150 + (120 * (speed / 120.0)); // Calculate angle based on speed
    double angleRad = qDegreesToRadians(angle);
    QPointF endPoint(pos.x() + 250 * qCos(angleRad), pos.y() + 250 * qSin(angleRad)); // Calculate end point of the needle
    painter.drawLine(pos, endPoint);                                                  // Draw the needle line
}

void Canvas::connection_set_status(bool status)
{
    connection_status = status;

    if (connection_status == false)
    {
        speedometer_set_speed(0); // Reset speed if disconnected

        indicator_set_left(false);
        indicator_set_right(false);
        indicator_set_visable(false);

        battery_set_level(0);
        thermometer_set_temperature(0);
    }
}

void Canvas::connection_draw(QPainter &painter, const QPoint &pos)
{
    if (connection_status)
    {
        painter.setPen(QColor(255, 255, 255));

        QChar qchar(0xe9e4);
        QFont font("MaterialIcons.ttf");
        font.setPointSize(connection_icon_size);
        painter.setFont(font);
        painter.drawText(pos.x(), pos.y(), QString(qchar));

        QFont temp_font("Arial");
        temp_font.setPointSize(connection_text_size);
        painter.setFont(temp_font);
        QString speedText = QString::number(speedometer_speed, 'f', 0) + " km/h"; // Format speed text
        QSize textSize = painter.fontMetrics().size(Qt::TextSingleLine, speedText);
        painter.setPen(Qt::white);                                                      // Set pen color for the text
        painter.drawText(pos.x() - textSize.width() / 2 + 35, pos.y() + 20, speedText); // Draw the speed text below the needle
    }
    else
    {
        painter.setPen(QColor(255, 0, 0));

        QChar qchar(0xe628);
        QFont font("MaterialIcons.ttf");
        font.setPointSize(connection_icon_size);
        painter.setFont(font);
        painter.drawText(pos.x(), pos.y(), QString(qchar));

        QFont temp_font("Arial");
        temp_font.setPointSize(connection_text_size);
        painter.setFont(temp_font);
        QString text = "Connection Error";
        QSize textSize = painter.fontMetrics().size(Qt::TextSingleLine, text);

        painter.drawText(pos.x() - textSize.width() / 2 + 35, pos.y() + 20, text); // Draw the speed text below the needle
    }
}

void Canvas::speedometer_set_speed(int val)
{
    int max = signal["speed"].max;
    int min = signal["speed"].min;
    speedometer_speed = std::clamp(val, min, max);
}

void Canvas::speedometer_draw(QPainter &painter, const QPoint &pos)
{
    painter.setRenderHint(QPainter::Antialiasing);

    // Set pen for the arc
    QPen arcPen(Qt::white, 10);
    painter.setPen(arcPen);

    QRectF rect(pos.x(), pos.y(), 600, 600);   // Bounding rectangle for arc
    painter.drawArc(rect, -30 * 16, 240 * 16); // Draw arc from -30° to 210°

    // Tick and text settings
    const int centerX = rect.center().x();
    const int centerY = rect.center().y();
    const int radiusOuter = 290 - 5; // Outer radius for tick marks
    int radiusInner = 275 - 5;       // Inner radius for tick marks
    const int textRadius = 235;      // Radius for text placement

    QPen tickPen(Qt::white, 7);
    QPen tickPen_thin(Qt::white, 3);
    QPen tickPen_middle(Qt::white, 5);

    painter.setPen(tickPen);
    QFont temp_font("Arial");
    temp_font.setPointSize(speedometer_font_size);
    painter.setFont(temp_font);

    int startAngle = 150;     // degrees
    int endAngle = 150 + 240; // degrees
    int numSteps = 13;        // For 0 to 120 in steps of 10

    radiusInner -= 5;

    for (int i = 0; i < numSteps; ++i)
    {
        painter.setPen(tickPen);

        int speed = i * 20;
        double angleDeg = startAngle + (endAngle - startAngle) * (double(i) / (numSteps - 1));
        double angleRad = qDegreesToRadians(angleDeg);

        // Tick line points
        QPointF outer(centerX + radiusOuter * qCos(angleRad),
                      centerY + radiusOuter * qSin(angleRad));
        QPointF inner(centerX + radiusInner * qCos(angleRad),
                      centerY + radiusInner * qSin(angleRad));

        painter.drawLine(inner, outer);

        // Text point (slightly inside the inner radius)
        QPointF textPoint(centerX + textRadius * qCos(angleRad),
                          centerY + textRadius * qSin(angleRad));

        QString label = QString::number(speed);
        QSize textSize = painter.fontMetrics().size(Qt::TextSingleLine, label);
        painter.drawText(textPoint.x() - textSize.width() / 2,
                         textPoint.y() + textSize.height() / 4,
                         label);
    }

    startAngle += 5;
    endAngle -= 5;
    radiusInner += 10;

    for (int i = 0; i < numSteps * 2 - 2; ++i)
    {
        painter.setPen(tickPen_thin);
        double angleDeg = startAngle + (endAngle - startAngle) * (double(i) / (numSteps * 2 - 3));
        double angleRad = qDegreesToRadians(angleDeg);

        // Tick line points
        QPointF outer(centerX + radiusOuter * qCos(angleRad),
                      centerY + radiusOuter * qSin(angleRad));
        QPointF inner(centerX + radiusInner * qCos(angleRad),
                      centerY + radiusInner * qSin(angleRad));

        painter.drawLine(inner, outer);
    }

    startAngle += 5;
    endAngle -= 5;
    radiusInner -= 5;

    for (int i = 0; i < numSteps - 1; ++i)
    {
        painter.setPen(tickPen_middle);
        double angleDeg = startAngle + (endAngle - startAngle) * (double(i) / (numSteps - 2));
        double angleRad = qDegreesToRadians(angleDeg);

        // Tick line points
        QPointF outer(centerX + radiusOuter * qCos(angleRad),
                      centerY + radiusOuter * qSin(angleRad));
        QPointF inner(centerX + radiusInner * qCos(angleRad),
                      centerY + radiusInner * qSin(angleRad));

        painter.drawLine(inner, outer);
    }

    needle_draw(painter, QPoint(centerX, centerY), speedometer_speed);
}

void Canvas::thermometer_set_temperature(int val)
{
    int max = signal["temperature"].max;
    int min = signal["temperature"].min;
    thermometer_value = std::clamp(val, min, max);
}

void Canvas::thermometer_draw(QPainter &painter, const QPoint &pos)
{
    // Color variables
    QColor temp_colors[3] = {QColor(255, 255, 255), QColor(0, 0, 255), QColor(255, 0, 0)}; // 0 = White, 1 = Blue, 2 = Red

    QColor color;
    if (thermometer_value < 5)
    {
        color = temp_colors[0]; // White for low temperatures
    }
    else if (thermometer_value < 40)
    {
        color = temp_colors[1]; // Blue for medium temperatures
    }
    else
    {
        color = temp_colors[2]; // Red for high temperatures
    }

    painter.setPen(color);

    // Print the QChar thermometer icon
    QChar qchar(0xe1ff);
    QFont font("MaterialIcons.ttf");
    font.setPointSize(thermometer_icon_size);
    painter.setFont(font);
    painter.drawText(pos.x(), pos.y(), QString(qchar));

    painter.setPen(QColor(255, 255, 255));

    // Draw the temperature value under the icon
    QString temp_text = QString::number(thermometer_value, 'f', 0) + " °C";
    QFont temp_font("Arial", thermometer_text_size);
    painter.setFont(temp_font);
    painter.drawText(pos.x() + thermometer_icon_size / 4, pos.y() + thermometer_text_size, temp_text);
}

Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    // Canvas minimum size
    setMinimumSize(2000, 2000);

    QObject::connect(&blink_timer, &QTimer::timeout, this, [this]()
                     {
                         blink_state = !blink_state; // Toggle blink state
                         indicator_set_visable(blink_state);
                         update(); // Request a repaint
                     });
    audioOutput.setVolume(1.0); // Set audio output volume

    player.setAudioOutput(&audioOutput);
    player.setSource(QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/sound.wav"));

    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, this,
                     [this](QMediaPlayer::MediaStatus status)
                     {
                         if (status == QMediaPlayer::EndOfMedia)
                         {
                             if (indicator_left || indicator_right)
                             {
                                 player.setSource(QUrl()); // reset completely
                                 player.setSource(QUrl::fromLocalFile(
                                     QCoreApplication::applicationDirPath() + "/sound.wav"));
                                 player.setPosition(0);
                                 player.play();
                             }
                         }
                     });

    blink_timer.start(440);
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QBrush brush;
    brush.setColor(QColor(0, 0, 0));
    painter.setBackground(brush);

    connection_draw(painter, QPoint(293, 500));
    thermometer_draw(painter, QPoint(700, 530));
    battery_draw(painter, QPoint(700, 425));
    speedometer_draw(painter, QPoint(25, 80));
    indicator_draw(painter, QPoint(50, 150), 475);
}
