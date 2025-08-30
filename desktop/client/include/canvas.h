#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include <QTimer>
#include <QtMultimedia/QMediaPlayer>
#include <QAudioOutput>

class Canvas : public QWidget
{

public:
    Canvas(QWidget *parent = nullptr);

    QMediaPlayer player;
    QAudioOutput audioOutput;

    /**
     * @brief Paint event to draw the canvas.
     * 
     * @param event 
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief Set the battery level.
     * 
     * @param level Level in percentage, range 0-100
     */
    void battery_set_level(int level);

    /**
     * @brief Draw the battery icon and level.
     * 
     * @param painter Painter to draw with
     * @param position QPoint where the battery icon should be drawn
     */
    void battery_draw(QPainter &painter, const QPoint &position);

    /**
     * @brief Set the left indicator state.
     * 
     * @param left 1 for on, 0 for off
     */
    void indicator_set_left(int left);

    /**
     * @brief Set the right indicator state.
     * 
     * @param right 1 for on, 0 for off
     */
    void indicator_set_right(int right);

    /**
     * @brief Set the visibility of the indicators.
     * 
     * @param _visible true to show, false to hide
     */
    void indicator_set_visable(bool _visible);

    /**
     * @brief Draw the indicators.
     * 
     * @param painter Painter to draw with
     * @param position Point where the indicators should be drawn
     * @param distance Distance between left and right indicators
     */
    void indicator_draw(QPainter &painter, const QPoint &position, const int distance);

    /**
     * @brief Set the speed for the speedometer.
     * 
     * @param val Speed value 0 - 240
     */
    void speedometer_set_speed(int speed);

    /**
     * @brief Draw the speedometer.
     * 
     * @param painter Painter to draw with
     * @param position Point where the speedometer should be drawn
     */
    void speedometer_draw(QPainter &painter, const QPoint &position);
    
    /**
     * @brief Draw the needle for the speedometer.
     * 
     * @param painter Painter to draw with
     * @param pos Point where the needle should be drawn
     * @param speed Speed value for the needle position
     */
    void needle_draw(QPainter &painter, const QPoint &pos, const double speed);

    /**
     * @brief Set the temperature for the thermometer.
     * 
     * @param celsius Temperature in Celsius, range -60 - 60
     */
    void thermometer_set_temperature(int celsius);

    /**
     * @brief Draw the thermometer.
     * 
     * @param painter Painter to draw with
     * @param position Position where the thermometer should be drawn
     */
    void thermometer_draw(QPainter &painter, const QPoint &position);

    /**
     * @brief Draw the connection status icon and speed.
     * 
     * @param painter Painter to draw with
     * @param pos Position to draw the connection icon
     */
    void connection_draw(QPainter &painter, const QPoint &pos);

    /**
     * @brief Set the connection status.
     * 
     * @param status Connection status (true = connected, false = disconnected)
     */
    void connection_set_status(bool status);

private:
    QTimer blink_timer = QTimer(this); // Timer for blinking effect
    bool blink_state = false;

    /**
     * @brief Update the audio playback for the indicators.
     * 
     */
    void updateIndicatorAudio(void);
};

#endif