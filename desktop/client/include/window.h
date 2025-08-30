#ifndef WINDOW_H
#define WINDOW_H

#include <QDialog>
#include <QBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include "canvas.h"
#include "comservice.h"
class Window : public QDialog
{
    
public:
    Window(COMService &com_service);

private:
    QTimer update_timer = QTimer(this);
    Canvas canvas;
};

#endif