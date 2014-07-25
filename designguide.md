# B+ ADD-ON BOARD / HAT DESIGN GUIDE

**NOTE THIS INFORMATION IS CURRENTLY STILL CHANGING**

## Using GPIO Pins

**NOTE** All references to GPIO numbers within this document are referring to the BCM2835 GPIOs (**NOT** pin numbers on the J8 GPIO header).

### Power-on State

In the new B+ firmware after power-on the bank 0 GPIOs on 40W GPIO header J8 (except ID_SD and ID_SC which are GPIO0 and 1 respectiveley) will be inputs with either a pull up or pull down. The default pull state can be found in the [BCM2835 peripherals specificaion](http://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2835/BCM2835-ARM-Peripherals.pdf) section 6.2 table 6-31 (see the "Pull" column).

### Notes and Recommendations

GPIO pins ID_SC and ID_SD (GPIO0 and GPIO1) are reserved for use solely for board detection / identification. **The only allowed connections to the ID_ pins are either an ID EEPROM plus pull up resistors OR ID_SD shorted to GND and ID_SC left unconnected (for boards without an EEPROM). Do not connect anything else to these pins!**

Raspberry Pi models A and B use some bank 0 GPIOs for board control functions and UART output:

    GPIO6 -> LAN_RUN
    GPIO14 -> UART_TX
    GPIO16 -> STATUS_LED

**If a user boots a B+ with legacy firmware these pins may get driven so an add-on board must avoid driving these, or use a current limiting resistor (or some other protection) if that is not possible. Note also that a board must not rely on the pull state of these pins during boot**

## ID EEPROM

Within the set of pins available on the J8 GPIO header, ID_SC and ID_SD (GPIO0/SCL and GPIO1/SDA) are reserved for board detection / identification. **The only allowed connections to the ID_ pins are either an ID EEPROM plus pull up resistors; OR ID_SD shorted to GND and ID_SC left unconnected (for boards without an EEPROM). Do not connect anything else to these pins!**

The ID EEPROM is interrogated at boot time and provides the Pi with the vendor information, the required GPIO setup (pin settings and functions) for the board as well as a binary Linux device tree fragment which also specifies which hardware is used and therefore which drivers need loading. EEPROM information is also available to userland Linux software for identifying attached boards (probably via a sysfs interface but this is TBD).

Pull-ups must be provided on the top board for ID_SC and ID_SD (SCL and SDA respectively) to 3V3. The required pull-up value is 3.9K.

**EEPROM Device Specification**

- 24Cxx type 3.3V I2C EEPROM must be used (some types are 5V only, do not use these).
- The EEPROM must be of the **16-bit** addressable type (**do not use ones with 8-bit addressing**)
- Do not use 'paged' type EEPROMs where the I2C lower address bit(s) select the EEPROM page.
- Only required to support 100kHz I2C mode.
- Devices that perform I2C clock stretching are not supported.
- Write protect pin must be supported and protect the entire device memory.

Note that due to the restrictions above (only using non-paged 16-bit addressable devices is allowed), many of the smaller I2C EEPROMs are ruled out - please check datasheets carefully when choosing a suitable EEPROM for your HAT.

A recommended part that satisfies the above constraints is OnSemi CAT24C32 which is a 32kbit (4kbyte) device. The minimum EEPROM size required is variable and depends on the size of the vendor data strings in the EEPROM and whether a device tree data blob is included (and its size) and whether any other vendor specific data is included.

It is recommended that EEPROM WP (write protect) pin be connected to a test point on the board and pulled up to 3V3 with a 1K resistor. The idea is that at board test/probe the EEPROM can be written (WP pin can be driven LOW), but there is no danger of a user accidentally changing the device contents once the board leaves the factory. Note that the recommended device has an internal pull down hence the stiff (1K) pull up is required. Note that on some devices WP does not write protect the entire array (e.g. some Microchip variants) – avoid using these.

It may be desirable for a board to have the ability for its EEPROM to be reflashed by an end user, in this case it is recommended to also include a user settable jumper, dip switch or other relatively simple method to short WP to GND and make the EEPROM writable once more. At least this way a user has to perform a specific action to make the EEPROM writeable again before being able to re-flash it and a suitable warning process can be put in place to make sure the correct image is used.

Address pins where present on a device should be set to zero. (NB reduced pin count variants of the recommended device – e.g. SOT23-5 package - usually have A[2:0] set to 0 anyway).

Details of the EEPROM data format can be found in the [EEPROM format specification](eeprom-format.md). [Software tools](./eepromutils) are available for creation of valid EEPROM images, to flash an image or read and dump and image to/from an attached ID EEPROM.

[The following schematic fragment](eeprom-circuit.png) is an example of connecting an EEPROM  including a jumper and probe point to disable write protect.

## Mechanical Specification

The [following drawing](hat-board-mechanical.pdf) gives the mechanical details for add-on boards which conform to the Raspberry Pi HAT specification.

## Back Powering the Pi via the J8 GPIO Header

It is possible to power the Pi by supplying 5V through the GPIO header pins 2,4 and GND. The acceptable input voltage range is 5V ±5%.

On the B+ Pi, the 5V GPIO header pins connect to the 5V net after the micro-USB input, polyfuse and input 'ideal' safety diode (made up of the PFET and matched PNP transistors). The 'safety' diode stops any appreciable current flowing back out of the 5V micro USB should the 5V net on the board be at a higher voltage than the 5V micro USB input.

If the add-on board uses any more GPIO connector pins than the first 26 (i.e. is designed for a B+) and provides back-powering via the 5V GPIO header pins it is required to implement a duplicate power safety diode before the HAT 5V net (which then feeds power back through the 5V GPIO pins). OR alternatively provide some other mechanism to guarantee that it is safe if both the Pi PSU and add-on board PSU are connected. It is still recommended to add this circuitry for new board designs that only implelent the first 26 pins of the GPIO header but that also implement back powering.

**Under no circumstances should a power source be connected to the J8 3.3V pins.**