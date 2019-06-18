![My image](http://lh5.ggpht.com/9oTbWlns_z06l4F1dMfok2qFM1x3eUGHGVsTNHLcVefKkMMb8AFx1xwWLg5GfmayKoBsiIt0EP2ZHzWsOwoy3g=s300)

# jjy-radio-receiver
Receives and parses the Japanese Low Frequency Time Signal broadcast from Mount Otakadoya. Allows automatic setting of a clock device.

## Requirements
This code runs on Atmel microcontrollers. It requires less than 1K of RAM and most of the work is interrupt driven, so 8MHz clocks are sufficient. It also requires scavenging an SM9501A radio receiver chip.

Details on the build can be found at http://jitsukuru.com/2014/12/pulling-time-from-air/
