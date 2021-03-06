# SparkFun Si4703 Raspberry Pi Library

![SparkFun Si4703](https://cdn.sparkfun.com//assets/parts/6/2/3/5/11083-02.jpg)

[*SparkFun Si4703 Breakout(BOB-11083)*](https://www.sparkfun.com/products/11083)

Basic functionality of the Si4703 FM tuner chip.
Allows user to tune to different FM stations, find station ID and song name, and
RDS and RBDS information.

## Repository Contents

* **/examples** - Sample program using the library to control the tuner chip.
* **/src** - Source files for the library (.cpp, .h).

## Usage
First install the [wiringPi](http://wiringpi.com/download-and-install) libary.

Then wire the breakout board to the RPi as follows:

  | Breakout Pin  | RPi Physical Pin                                              |
  | ------------- | ------------------------------------------------------------- |
  | VCC           | [Pin 1](https://pinout.xyz/pinout/pin1_3v3_power)             |
  | SDIO          | [Pin 3](https://pinout.xyz/pinout/pin3_gpio2)                 |
  | SCLK          | [Pin 5](https://pinout.xyz/pinout/pin5_gpio3)                 |
  | GND           | [Pin 9](https://pinout.xyz/pinout/ground) (or any ground pin) |
  | RST           | [Pin 15](https://pinout.xyz/pinout/pin15_gpio22)              |

Then clone this source repository:

```bash
git clone https://github.com/sparkfun/Si4703_FM_Tuner_Evaluation_Board.git
```

Finally build and run the sample program:

```bash
cd Si4703_FM_Tuner_Evaluation_Board/Libraries/RaspberryPi
make run
```

## Products that use this Library 
* [SparkFun FM Tuner Evaluation Board](https://www.sparkfun.com/products/10663)- Evaluation
  board for Si4703. Includes audio jack. 
* [SparkFun FM Tuner Basic Breakout](https://www.sparkfun.com/products/11083)- Basic
  breakout of Si4703.


## License Information

This product is _**open source**_! 

Distributed as-is; no warranty is given.

- Your friends at SparkFun.

_The original SparkFun code was modified by Simon Monk. Thanks Simon!_

The Raspberry Pi
src/SparkFunSi4703.cpp and src/SparkFunSi4703.cpp are open source so please feel
free to do anything you want with it; you buy me a beer if you use this and we
meet someday. 
(<a href="http://en.wikipedia.org/wiki/Beerware">Beerware license</a>)
