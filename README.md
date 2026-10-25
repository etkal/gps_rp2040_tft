# GPS_RP2040_TFT_cpp
GPS implementation in C++ using RP2040 with TFT display

- Background

  For some time I have been experimenting with GPS modules and NMEA sentence parsing.  Originally I designed the project using Micropython, but have more recently developed this C++ implementation by creating modules to abstract the GPS module, an LED for indication, and a display device.
  
- Hardware

  Raspberry Pi Pico RP2040-based microcontroller with UART and SPI ports, along with a simple 3.3 volt GPS module (in this implementation it's an AdaFruit GPS Breakout board), and in this case a 320x240 ili9341 TFT display.  The LED can be onboard the microcontroller or external.
  
  Make sure to specify the correct board in the top level CMakeLists.txt; in this implementation the Raspberry Pi Pico, Pico W, SeeedStudio XIAO RP2040 and Waveshare RP2040-Zero have been tested.  WS2812 LEDs are supported, as well as onboard and GPIO-connected simple LEDs.

  I also have a separate OLED implementation that uses an SSD1306 128x64 mono display.

- Software Requirements

  This implementation uses the Raspberry Pi Pico RP2040 controller C++ SDK:
  https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html

- Operation

  In main.cpp the required abstraction objects are created, and the program reads NMEA 0183 sentences from the GPS UART port, displaying various information on the TFT display. Either the UART processing or the TFT display/touch can be launched on core 1 if you'd like to more fully utilize the RP2040/RP2350 multicore capabilities.

  In the high level CMakeLists.txt some specification is required (e.g. platform, display, tuning, time zone).

  In main.cpp various SPI and GPIO pin specifics are coded, everythong is highly customizable.

  The data is correlated and displayed in textual and graphical form on the display.  For the TFT it displays the latitude, longitude, altitude, GMT time and an indication of the number of satellites and fix type.  A graphical representation of the satellite positions is displayed, as well as a satellite signal strength bar graph and a clock.

  An LED blinks to indicate the presence of a fix.  If a WS2812 LED is available, colors are used to indicate additional information, e.g. blink red for no fix, green for a fix using the GPS module onboard antenna, blue for external antenna; customization may be needed for the specific GPS module and LED.

  When experimenting with larger displays (e.g. 3.5" 480x320) I found that there is not enough memory to support a full framebuf for the entire screen.  Without using the framebuf (i.e. direct hardware access for draw operations) performance was pitiful and the app useless.  To accommodate I redesigned using a smaller framebuf covering half or quarter of the display, then writing the overall image to the buffer, in effect shifting it to the proper quadrant, then blitting the framebuf to the appropriate screen portion.  Even though the full display write operation is performed multiple times, with pixels outside the buffer (for the current quadrant) being ignored, performance is not an issue.  For this application the screen is redrawn once per second, so this occupies only 8% of the total application.  More complicated effects such as animation might require further optimization. In the latest version I have implemented an optimized lookup for the colors with an assumption that only 16 distinct colors are needed. In CMakeLists.txt you can indicate which framebuffer encoding to use and the number of quadrants. This way trading off performance vs memory usage is adjustable.

  I have now also added support for the XPT2046 resistive touch pad controller, so you can define screen areas that can be tapped and perform actions based on the area touched. This is still pretty basic but functional. Some displays such as the WaveShare Pico-Restouch-LCD-3.5 are hard-wired to share the SPI1 bus among the integrated TFT display, touch and SD card controllers, so the application dynamically switches SPI bus speed if needed to handle the much slower touch pad.

  For additional performance enhancement, customization of the system clock and SPI bus speed is supported. For higher speeds the core voltage is increased slightly. I find the WaveShare Pico-Restouch-LCD-3.5 is the worst at higher speeds, the color stability is only good up to 36MHz SPI speed (set 144MHz system clock for an even multiple). Other TFT displays have been fine even at 62.5MHz SPI, your mileage may vary.

- Additional fonts

  This project supports dedicated bitmap header fonts as well as integer scaling of fonts.
  - Petme 8x8
  - Terminus 6x12
  - Terminus 8x14
  - Terminus 10x18
  - Terminus 12x24
  - Terminus 16x32

- Enjoy!!

![Project board showing RP2040, GPS module and ili9341 TFT display](images/GPS_TFT.jpg)
