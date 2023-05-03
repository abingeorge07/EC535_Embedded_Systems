#include "rasterwindow.h"


//! [1]
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    RasterWindow window;

    window.show();

    return app.exec();
}
//! [1]
